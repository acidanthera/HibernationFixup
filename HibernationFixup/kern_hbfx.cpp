//
//  kern_hbfx.cpp
//  HibernationFixup
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#include <Library/LegacyIOService.h>
#include "LegacyRootDomain.h"

#include <Headers/kern_api.hpp>
#include <Headers/kern_file.hpp>
#include <Headers/kern_compat.hpp>
#include <Headers/kern_compression.hpp>
#include <Headers/kern_iokit.hpp>
#include <Headers/kern_rtc.hpp>

#include "kern_config.hpp"
#include "kern_hbfx.hpp"

// Only used in apple-driven callbacks
static HBFX *callbackHBFX = nullptr;

// gIOHibernateState, kIOHibernateStateKey
enum
{
	kIOHibernateStateInactive            = 0,
	kIOHibernateStateHibernating         = 1,	/* writing image */
	kIOHibernateStateWakingFromHibernate = 2	/* booted and restored image */
};

enum
{
	kMachineRestoreBridges      = 0x00000001,
	kMachineRestoreEarlyDevices = 0x00000002,
	kMachineRestoreDehibernate  = 0x00000004,
	kMachineRestoreTunnels      = 0x00000008,
};

/* Command register definitions */
enum {
	kIOPCICommandIOSpace                = 0x0001,
	kIOPCICommandMemorySpace            = 0x0002,
	kIOPCICommandBusMaster              = 0x0004,
	kIOPCICommandSpecialCycles          = 0x0008,
	kIOPCICommandMemWrInvalidate        = 0x0010,
	kIOPCICommandPaletteSnoop           = 0x0020,
	kIOPCICommandParityError            = 0x0040,
	kIOPCICommandAddressStepping        = 0x0080,
	kIOPCICommandSERR                   = 0x0100,
	kIOPCICommandFastBack2Back          = 0x0200,
	kIOPCICommandInterruptDisable       = 0x0400
};

static const char *kextIOPCIFamilyPath[] { "/System/Library/Extensions/IOPCIFamily.kext/IOPCIFamily" };

static KernelPatcher::KextInfo kextIOPCIFamily {
	"com.apple.iokit.IOPCIFamily", kextIOPCIFamilyPath, arrsize(kextIOPCIFamilyPath), {true}, {}, KernelPatcher::KextInfo::Unloaded
};

//==============================================================================

bool HBFX::init()
{
	callbackHBFX = this;

	lilu.onPatcherLoadForce(
	[](void *user, KernelPatcher &patcher) {
		callbackHBFX->processKernel(patcher);
	}, this);

	lilu.onKextLoadForce(&kextIOPCIFamily, 1,
	[](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
		callbackHBFX->processKext(patcher, index, address, size);
	}, this);

	return true;
}

//==============================================================================

void HBFX::deinit()
{
	nvstorage.deinit();
}

//==============================================================================

IOReturn HBFX::IOHibernateSystemSleep(void)
{
	IOReturn result = FunctionCast(IOHibernateSystemSleep, callbackHBFX->orgIOHibernateSystemSleep)();
	DBGLOG("HBFX", "IOHibernateSystemSleep is called, result is: %x", result);

	uint32_t ioHibernateState = kIOHibernateStateInactive;
	OSData *data = OSDynamicCast(OSData, IOService::getPMRootDomain()->getProperty(kIOHibernateStateKey));
	if (data != nullptr)
	{
		ioHibernateState = *((uint32_t *)data->getBytesNoCopy());
		DBGLOG("HBFX", "Current hibernate state from IOPMRootDomain is: %d", ioHibernateState);
	}

	if (result == KERN_SUCCESS || ioHibernateState == kIOHibernateStateHibernating)
	{
		OSData *rtc = OSDynamicCast(OSData, IOService::getPMRootDomain()->getProperty(kIOHibernateRTCVariablesKey));
		if (rtc && !callbackHBFX->nvstorage.exists(kIOHibernateRTCVariablesKey))
		{
			if (!callbackHBFX->nvstorage.write(kIOHibernateRTCVariablesKey, rtc, NVStorage::OptRaw))
				SYSLOG("HBFX", "IOHibernateRTCVariablesKey can't be written to NVRAM.");
		}

		OSData *smc = OSDynamicCast(OSData, IOService::getPMRootDomain()->getProperty(kIOHibernateSMCVariablesKey));
		if (smc && !callbackHBFX->nvstorage.exists(kIOHibernateSMCVariablesKey))
		{
			if (!callbackHBFX->nvstorage.write(kIOHibernateSMCVariablesKey, smc, NVStorage::OptRaw))
				SYSLOG("HBFX", "IOHibernateSMCVariablesKey can't be written to NVRAM.");
		}

		if (ADDPR(hbfx_config).dumpNvram)
		{
			uint32_t size;
			if (uint8_t *buf = callbackHBFX->nvstorage.read(kGlobalBoot0082Key, size, NVStorage::OptRaw))
			{
				if (!callbackHBFX->nvstorage.write(kBoot0082Key, buf, size, NVStorage::OptRaw))
					SYSLOG("HBFX", "%s can't be written!", kBoot0082Key);
				Buffer::deleter(buf);
			}
			else
				SYSLOG("HBFX", "Variable %s can't be found!", kBoot0082Key);

			if (uint8_t *buf = callbackHBFX->nvstorage.read(kGlobalBootNextKey, size, NVStorage::OptRaw))
			{
				if (!callbackHBFX->nvstorage.write(kBootNextKey, buf, size, NVStorage::OptRaw))
					SYSLOG("HBFX", "%s can't be written!", kBootNextKey);
				Buffer::deleter(buf);
			}
			else
				SYSLOG("HBFX", "Variable %s can't be found!", kBootNextKey);

			callbackHBFX->nvstorage.save(FILE_NVRAM_NAME);

			if (callbackHBFX->sync)
				callbackHBFX->sync(kernproc, nullptr, nullptr);

			callbackHBFX->nvstorage.remove(kBoot0082Key);
			callbackHBFX->nvstorage.remove(kBootNextKey);
		}
	}

	return result;
}

//==============================================================================

bool HBFX::IOPMrootDomain_getHibernateSettings(IOPMrootDomain *that, unsigned int *hibernateModePtr, unsigned int *hibernateFreeRatio, unsigned int *hibernateFreeTime)
{
	bool result = FunctionCast(IOPMrootDomain_getHibernateSettings, callbackHBFX->orgGetHibernateSettings)(that, hibernateModePtr, hibernateFreeRatio, hibernateFreeTime);
	if (!result)
		SYSLOG("HBFX", "orgGetHibernateSettings returned false");
	
	while (callbackHBFX->power_source && result && hibernateModePtr && OSDynamicCast(OSBoolean, that->getProperty(kAppleSleepDisabled)) == kOSBooleanFalse) {
		auto autoHibernateMode = ADDPR(hbfx_config).autoHibernateMode;
		bool whenLidIsClosed = (autoHibernateMode & Configuration::WhenLidIsClosed);
		bool whenExternalPowerIsDisconnected = (autoHibernateMode & Configuration::WhenExternalPowerIsDisconnected);
		bool whenBatteryIsNotCharging = (autoHibernateMode & Configuration::WhenBatteryIsNotCharging);
		bool whenBatteryIsAtWarnLevel = (autoHibernateMode & Configuration::WhenBatteryIsAtWarnLevel);
		bool whenBatteryAtCriticalLevel = (autoHibernateMode & Configuration::WhenBatteryAtCriticalLevel);
	
		if (whenLidIsClosed && OSDynamicCast(OSBoolean, that->getProperty(kAppleClamshellStateKey)) != kOSBooleanTrue) {
			DBGLOG("HBFX", "Auto hibernate: clamshell is open, do not force to hibernate");
			break;
		}
		
		if (callbackHBFX->power_source->batteryInstalled()) {
			if (whenExternalPowerIsDisconnected && callbackHBFX->power_source->externalConnected()) {
				DBGLOG("HBFX", "Auto hibernate: external is connected, do not force to hibernate");
				break;
			}
			
			if (whenBatteryIsNotCharging && callbackHBFX->power_source->isCharging()) {
				DBGLOG("HBFX", "Auto hibernate: battery is charging, do not force to hibernate");
				break;
			}
			
			if (whenBatteryIsAtWarnLevel && !callbackHBFX->power_source->atWarnLevel()) {
				DBGLOG("HBFX", "Auto hibernate: battery is not at warning level, do not force to hibernate");
				break;
			}
			
			if (whenBatteryAtCriticalLevel && !callbackHBFX->power_source->atCriticalLevel()) {
				DBGLOG("HBFX", "Auto hibernate: battery is not at critical level, do not force to hibernate");
				break;
			}
		}

		*hibernateModePtr = 25;
		DBGLOG("HBFX", "Auto hibernate: force hibernate mode to 25");
		break;
	}
	
	return result;
}

//==============================================================================

IOReturn HBFX::IOPMrootDomain_setMaintenanceWakeCalendar(IOPMrootDomain *that, IOPMCalendarStruct *calendar)
{
	IOReturn result = FunctionCast(IOPMrootDomain_setMaintenanceWakeCalendar, callbackHBFX->orgSetMaintenanceWakeCalendar)(that, calendar);
	if (result != KERN_SUCCESS)
		SYSLOG("HBFX", "orgSetMaintenanceWakeCalendar returned error 0x%x", result);
	else if (calendar != nullptr)
	{
		DBGLOG("HBFX", "Maintenance event was set to %02d.%02d.%04d  %02d:%02d:%02d, selector = %d",
			   calendar->day, calendar->month, calendar->year, calendar->hour, calendar->minute, calendar->second, calendar->selector);
	}
	
	return result;
}

//==============================================================================

int HBFX::packA(char *inbuf, uint32_t length, uint32_t buflen)
{
	char key[128];
	unsigned int bufpos = 0;
	if (callbackHBFX->orgPackA)
		bufpos = FunctionCast(packA, callbackHBFX->orgPackA)(inbuf, length, buflen);
	
	if (callbackHBFX->ml_at_interrupt_context && !callbackHBFX->ml_at_interrupt_context())
	{
		const bool interrupts_enabled = callbackHBFX->ml_get_interrupts_enabled();
		if (!interrupts_enabled)
			callbackHBFX->ml_set_interrupts_enabled(TRUE);
		if (callbackHBFX->ml_get_interrupts_enabled())
		{
			int counter = 10;
			const bool preemption_enabled = callbackHBFX->preemption_enabled();
			while (!callbackHBFX->preemption_enabled() && --counter >= 0)
			{
				callbackHBFX->enable_preemption();
				IOSleep(1);
			}
			
			if (callbackHBFX->preemption_enabled())
			{
				unsigned int pi_size = bufpos ? bufpos : length;
				const unsigned int max_size = 768;
				counter = 0;
				while (pi_size > 0)
				{
					unsigned int part_size = (pi_size > max_size) ? max_size : pi_size;
					snprintf(key, sizeof(key), "AAPL,PanicInfo%04d", counter++);
					callbackHBFX->nvstorage.write(key, reinterpret_cast<const uint8_t*>(inbuf), part_size, NVStorage::OptRaw);
					pi_size -= part_size;
					inbuf += part_size;
				}
				
				callbackHBFX->nvstorage.save(FILE_NVRAM_NAME);
				callbackHBFX->sync(kernproc, nullptr, nullptr);
			}
			
			if (!preemption_enabled)
				callbackHBFX->disable_preemption();
			if (!interrupts_enabled)
				callbackHBFX->ml_set_interrupts_enabled(FALSE);
		}
	}
	
	return bufpos;
}

//==============================================================================

IOReturn HBFX::restoreMachineState(IOService *that, IOOptionBits options, IOService * device)
{
	if (kMachineRestoreDehibernate & options)
		callbackHBFX->correct_pci_config_command = true;

	IOReturn result = FunctionCast(restoreMachineState, callbackHBFX->orgRestoreMachineState)(that, options, device);

	if (kMachineRestoreDehibernate & options)
		callbackHBFX->correct_pci_config_command = false;
	
	return result;
}

//==============================================================================

void HBFX::extendedConfigWrite16(IOService *that, UInt64 offset, UInt16 data)
{
	if (callbackHBFX->correct_pci_config_command && offset == WIOKit::PCIRegister::kIOPCIConfigCommand)
	{
		if (strlen(ADDPR(hbfx_config).ignored_device_list) == 0 || strstr(ADDPR(hbfx_config).ignored_device_list, that->getName()) != nullptr)
		{
			if (!(data & kIOPCICommandMemorySpace))
			{
				DBGLOG("HBFX", "HBFX will add flag kIOPCICommandMemorySpace for deivce %s, offset = %08llX, data = %04X", that->getName(), offset, data);
				data |= kIOPCICommandMemorySpace;
			}
		}
	}

	FunctionCast(extendedConfigWrite16, callbackHBFX->orgExtendedConfigWrite16)(that, offset, data);
}

//==============================================================================

void HBFX::processKernel(KernelPatcher &patcher)
{
	if ((ADDPR(hbfx_config).autoHibernateMode & Configuration::EnableAutoHibenation) && !(progressState & ProcessingState::KernelRouted))
	{
		auto matching = IOService::serviceMatching("IOPMPowerSource");
		if (matching) {
			power_source = OSDynamicCast(IOPMPowerSource, IOService::copyMatchingService(matching));
			matching->release();
			if (!power_source) {
				SYSLOG("HBFX", "failed to get IOPMPowerSource");
			}
		} else {
			SYSLOG("HBFX", "failed to allocate IOPMPowerSource service matching");
		}
		
		if (power_source) {
			KernelPatcher::RouteRequest requests[] = {
				{ "__ZN14IOPMrootDomain20getHibernateSettingsEPjS0_S0_", IOPMrootDomain_getHibernateSettings, orgGetHibernateSettings },
				{ "__ZN14IOPMrootDomain26setMaintenanceWakeCalendarEPK18IOPMCalendarStruct", IOPMrootDomain_setMaintenanceWakeCalendar, orgSetMaintenanceWakeCalendar }
			};
			patcher.routeMultiple(KernelPatcher::KernelID, requests);
		}
	}
	
	if (ADDPR(hbfx_config).dumpNvram == false && checkRTCExtendedMemory())
	{
		DBGLOG("HBFX", "all kernel patches will be skipped since the second bank of RTC memory is available");
		return;
	}
	
	if (!initialize_nvstorage())
		return;
	
	if (!(progressState & ProcessingState::KernelRouted))
	{
		KernelPatcher::RouteRequest request { "_IOHibernateSystemSleep", IOHibernateSystemSleep, orgIOHibernateSystemSleep };
		patcher.routeMultiple(KernelPatcher::KernelID, &request, 1);
		
		ml_at_interrupt_context = reinterpret_cast<t_ml_at_interrupt_context>(patcher.solveSymbol(KernelPatcher::KernelID, "_ml_at_interrupt_context"));
		if (!ml_at_interrupt_context)
		{
			SYSLOG("HBFX", "failed to resolve _ml_at_interrupt_context %d", patcher.getError());
			patcher.clearError();
		}
		
		ml_get_interrupts_enabled = reinterpret_cast<t_ml_get_interrupts_enabled>(patcher.solveSymbol(KernelPatcher::KernelID, "_ml_get_interrupts_enabled"));
		if (!ml_get_interrupts_enabled)
		{
			SYSLOG("HBFX", "failed to resolve _ml_get_interrupts_enabled %d", patcher.getError());
			patcher.clearError();
		}

		ml_set_interrupts_enabled = reinterpret_cast<t_ml_set_interrupts_enabled>(patcher.solveSymbol(KernelPatcher::KernelID, "_ml_set_interrupts_enabled"));
		if (!ml_set_interrupts_enabled)
		{
			SYSLOG("HBFX", "failed to resolve _ml_set_interrupts_enabled %d", patcher.getError());
			patcher.clearError();
		}

		if (ADDPR(hbfx_config).dumpNvram)
		{
			sync = reinterpret_cast<t_sync>(patcher.solveSymbol(KernelPatcher::KernelID, "_sync"));
			if (!sync) {
				SYSLOG("HBFX", "failed to resolve _sync %d", patcher.getError());
				patcher.clearError();
			}
			
			preemption_enabled = reinterpret_cast<t_preemption_enabled>(patcher.solveSymbol(KernelPatcher::KernelID, "_preemption_enabled"));
			if (!preemption_enabled) {
				SYSLOG("HBFX", "failed to resolve _preemption_enabled %d", patcher.getError());
				patcher.clearError();
			}
			
			enable_preemption = reinterpret_cast<t_enable_preemption>(patcher.solveSymbol(KernelPatcher::KernelID, "__enable_preemption"));
			if (!enable_preemption) {
				SYSLOG("HBFX", "failed to resolve __enable_preemption %d", patcher.getError());
				patcher.clearError();
			}
			
			disable_preemption = reinterpret_cast<t_disable_preemption>(patcher.solveSymbol(KernelPatcher::KernelID, "__disable_preemption"));
			if (!disable_preemption) {
				SYSLOG("HBFX", "failed to resolve __disable_preemption %d", patcher.getError());
				patcher.clearError();
			}
			
			if (sync && preemption_enabled && enable_preemption && disable_preemption && ml_at_interrupt_context &&
				ml_get_interrupts_enabled && ml_set_interrupts_enabled)
			{
				KernelPatcher::RouteRequest request {"_packA", packA, orgPackA};
				patcher.routeMultiple(KernelPatcher::KernelID, &request, 1);
			}
		}
		
		progressState |= ProcessingState::KernelRouted;
	}

	// Ignore all the errors for other processors
	patcher.clearError();
}

//==============================================================================

void HBFX::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size)
{
	if (!initialize_nvstorage())
		return;
	
	if (progressState != ProcessingState::EverythingDone)
	{
		if (ADDPR(hbfx_config).patchPCIFamily && kextIOPCIFamily.loadIndex == index &&
			!(progressState & ProcessingState::IOPCIFamilyRouted))
		{
			KernelPatcher::RouteRequest requests[] {
				{"__ZN11IOPCIBridge19restoreMachineStateEjP11IOPCIDevice", restoreMachineState, orgRestoreMachineState},
				{"__ZN11IOPCIDevice21extendedConfigWrite16Eyt", extendedConfigWrite16, orgExtendedConfigWrite16},
			};

			patcher.routeMultiple(index, requests, address, size);
			progressState |= ProcessingState::IOPCIFamilyRouted;
		}
	}
}

//==============================================================================

bool HBFX::checkRTCExtendedMemory()
{
	bool result = false;
	RTCStorage rtc_storage;
	if (rtc_storage.init())
	{
		result = rtc_storage.checkExtendedMemory();
		rtc_storage.deinit();
	}
	
	return result;
}

//==============================================================================

bool HBFX::initialize_nvstorage()
{
	static bool nvstorage_initialized = false;
	if (!nvstorage_initialized)
	{
		if (nvstorage.init())
		{
			DBGLOG("HBFX", "NVStorage was initialized");
			
			nvstorage_initialized = true;
			
			if (!ADDPR(hbfx_config).dumpNvram)
			{
				OSData *data = nvstorage.read("EmuVariableUefiPresent", NVStorage::OptRaw);
				if (data && data->isEqualTo(OSString::withCStringNoCopy("Yes")))
				{
					DBGLOG("HBFX", "EmuVariableUefiPresent is detected, set dumpNvram to true");
					ADDPR(hbfx_config).dumpNvram = true;
				}
				OSSafeReleaseNULL(data);
			}
			
#if defined(DEBUG) && defined(DEBUG_NVSTORAGE)
			// short NVStorage test
			uint8_t value[] = {0x01, 0x02, 0x03, 0x04, 0xFF, 0x04, 0x03, 0x02, 0x01,
							   0x01, 0x02, 0x03, 0x04, 0xFF, 0x04, 0x03, 0x02, 0x01,
							   0x01, 0x02, 0x03, 0x04, 0xFF, 0x04, 0x03, 0x02, 0x01,
							   0x01, 0x02, 0x03, 0x04, 0xFF, 0x04, 0x03, 0x02, 0x01,
							   0x01, 0x02, 0x03, 0x04, 0xFF, 0x04, 0x03, 0x02, 0x01};
			
			uint8_t enckey[] = {0xFF, 0x10, 0x08, 0x04, 0x02, 0x05, 0x09};
			uint32_t size = 0, dstlen = 1024;
			uint8_t *buf, *compressed_data, *decompressed_data;
			
			PANIC_COND((compressed_data = Compression::compress(Compression::ModeLZSS, dstlen, value, sizeof(value), nullptr)) == nullptr, "HBFX", "Compression::compress failed");
			PANIC_COND((decompressed_data = Compression::decompress(Compression::ModeLZSS, sizeof(value), compressed_data, dstlen)) == nullptr, "HBFX", "Compression::decompress failed");
			PANIC_COND(memcmp(decompressed_data, value, sizeof(value)) != 0, "HBFX", "memory is different from original");
			Buffer::deleter(decompressed_data);
			Buffer::deleter(compressed_data);
			
			dstlen = sizeof(value);
			PANIC_COND((compressed_data = nvstorage.compress(value, dstlen)) == nullptr, "HBFX", "NVStorage.compress failed");
			PANIC_COND((decompressed_data = nvstorage.decompress(compressed_data, dstlen)) == nullptr, "HBFX", "NVStorage.decompress failed");
			PANIC_COND(memcmp(decompressed_data, value, sizeof(value)) != 0, "HBFX", "memory is different from original");
			Buffer::deleter(decompressed_data);
			Buffer::deleter(compressed_data);
			
			const char* key = "NVStorageTestVar1";
			PANIC_COND(!nvstorage.write(key, value, sizeof(value), NVStorage::OptChecksum, enckey), "HBFX", "write failed for %s", key);
			PANIC_COND(!nvstorage.exists(key), "HBFX", "exists failed");
			PANIC_COND((buf = nvstorage.read(key, size, NVStorage::OptAuto, enckey)) == nullptr, "HBFX", "read failed for %s", key);
			PANIC_COND(size != sizeof(value), "HBFX", "read returned failed size for %s", key);
			PANIC_COND(memcmp(buf, value, sizeof(value)) != 0, "HBFX", "memory is different from original for %s", key);
			nvstorage.remove(key);
			
			key = "NVStorageTestVar2";
			PANIC_COND(!nvstorage.write(key, value, sizeof(value), NVStorage::OptChecksum | NVStorage::OptCompressed, enckey), "HBFX", "write failed for %s", key);
			PANIC_COND(!nvstorage.exists(key), "HBFX", "exists failed");
			PANIC_COND((buf = nvstorage.read(key, size, NVStorage::OptAuto, enckey)) == nullptr, "HBFX", "read failed for %s", key);
			PANIC_COND(size != sizeof(value), "HBFX", "read returned failed size for %s", key);
			PANIC_COND(memcmp(buf, value, sizeof(value)) != 0, "HBFX", "memory is different from original for %s", key);
			nvstorage.remove(key);
			
			key = "NVStorageTestVar3";
			PANIC_COND(!nvstorage.write(key, value, sizeof(value), NVStorage::OptChecksum | NVStorage::OptEncrypted, enckey), "HBFX", "write failed for %s", key);
			PANIC_COND(!nvstorage.exists(key), "HBFX", "exists failed for %s", key);
			PANIC_COND((buf = nvstorage.read(key, size, NVStorage::OptAuto, enckey)) == nullptr, "HBFX", "read failed for %s", key);
			PANIC_COND(size != sizeof(value), "HBFX", "read returned failed size for %s", key);
			PANIC_COND(memcmp(buf, value, sizeof(value)) != 0, "HBFX", "memory is different from original for %s", key);
			nvstorage.remove(key);
			
			key = "NVStorageTestVar4";
			PANIC_COND(!nvstorage.write(key, value, sizeof(value), NVStorage::OptChecksum | NVStorage::OptEncrypted | NVStorage::OptCompressed, enckey), "HBFX", "write failed for %s", key);
			PANIC_COND(!nvstorage.exists(key), "HBFX", "exists failed for %s", key);
			PANIC_COND((buf = nvstorage.read(key, size, NVStorage::OptAuto, enckey)) == nullptr, "HBFX", "read failed for %s", key);
			PANIC_COND(size != sizeof(value), "HBFX", "read returned failed size for %s", key);
			PANIC_COND(memcmp(buf, value, sizeof(value)) != 0, "HBFX", "memory is different from original for %s", key);
			nvstorage.remove(key);
			
			key = "NVStorageTestVar5";
			PANIC_COND(!nvstorage.write(key, value, sizeof(value), NVStorage::OptAuto, enckey), "HBFX", "write failed for %s", key);
			PANIC_COND(!nvstorage.exists(key), "HBFX", "exists failed for %s", key);
			PANIC_COND((buf = nvstorage.read(key, size, NVStorage::OptAuto, enckey)) == nullptr, "HBFX", "read failed for %s", key);
			PANIC_COND(size != sizeof(value), "HBFX", "read returned failed size for %s", key);
			PANIC_COND(memcmp(buf, value, sizeof(value)) != 0, "HBFX", "memory is different from original for %s", key);
			nvstorage.remove(key);
			
			key = "NVStorageTestVar6";
			PANIC_COND(!nvstorage.write(key, value, sizeof(value), NVStorage::OptCompressed, enckey), "HBFX", "write failed for %s", key);
			PANIC_COND(!nvstorage.exists(key), "HBFX", "exists failed for %s", key);
			PANIC_COND((buf = nvstorage.read(key, size, NVStorage::OptAuto, enckey)) == nullptr, "HBFX", "read failed for %s", key);
			PANIC_COND(size != sizeof(value), "HBFX", "read returned failed size for %s", key);
			PANIC_COND(memcmp(buf, value, sizeof(value)) != 0, "HBFX", "memory is different from original for %s", key);
			nvstorage.remove(key);
			
			key = "NVStorageTestVar7";
			PANIC_COND(!nvstorage.write(key, value, sizeof(value), NVStorage::OptEncrypted, enckey), "HBFX", "write failed for %s", key);
			PANIC_COND(!nvstorage.exists(key), "HBFX", "exists failed for %s", key);
			PANIC_COND((buf = nvstorage.read(key, size, NVStorage::OptAuto, enckey)) == nullptr, "HBFX", "read failed for %s", key);
			PANIC_COND(size != sizeof(value), "HBFX", "read returned failed size for %s", key);
			PANIC_COND(memcmp(buf, value, sizeof(value)) != 0, "HBFX", "memory is different from original for %s", key);
			nvstorage.remove(key);
			
			key = "NVStorageTestVar8";
			PANIC_COND(!nvstorage.write(key, value, sizeof(value), NVStorage::OptRaw, enckey), "HBFX", "write failed for %s", key);
			PANIC_COND(!nvstorage.exists(key), "HBFX", "exists failed for %s", key);
			PANIC_COND((buf = nvstorage.read(key, size, NVStorage::OptRaw, enckey)) == nullptr, "HBFX", "read failed for %s", key);
			PANIC_COND(size != sizeof(value), "HBFX", "read returned failed size for %s", key);
			PANIC_COND(memcmp(buf, value, sizeof(value)) != 0, "HBFX", "memory is different from original for %s", key);
			nvstorage.remove(key);
			
			key = "NVStorageTestVar9";
			OSData *data = OSData::withBytes("some string to be written in nvram", 32);
			OSData *newdata;
			PANIC_COND(!nvstorage.write(key, data, NVStorage::OptRaw, enckey), "HBFX", "write failed for %s", key);
			PANIC_COND(!nvstorage.exists(key), "HBFX", "exists failed for %s", key);
			PANIC_COND((newdata = nvstorage.read(key, NVStorage::OptRaw, enckey)) == nullptr, "HBFX", "read failed for %s", key);
			PANIC_COND(!newdata->isEqualTo(data), "HBFX", "memory is different from original for %s", key);
			data->release();
			newdata->release();

			DBGLOG("HBFX", "tests were finished");
#endif
		}
		else
		{
			SYSLOG("HBFX", "failed to initialize NVStorage");
		}
	}
	
	return nvstorage_initialized;
}
