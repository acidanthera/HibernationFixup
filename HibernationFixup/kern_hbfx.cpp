//
//  kern_hbfx.cpp
//  HibernationFixup
//
//  Copyright © 2020 lvs1974. All rights reserved.
//

#include <IOKit/IOService.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <IOKit/IOTimerEventSource.h>

#include <Headers/kern_api.hpp>
#include <Headers/kern_efi.hpp>
#include <Headers/kern_file.hpp>
#include <Headers/kern_compat.hpp>
#include <Headers/kern_compression.hpp>
#include <Headers/kern_iokit.hpp>
#include <Headers/kern_rtc.hpp>

#include "kern_config.hpp"
#include "kern_hbfx.hpp"

#include <kern/clock.h>
#include "gmtime.h"


#define FILE_NVRAM_NAME                 "/nvram.plist"
#define BACKUP_FILE_NVRAM_NAME          "/System/Volumes/Data/nvram.plist"

// Only used in apple-driven callbacks
static HBFX *callbackHBFX = nullptr;

static const char *kextIOPCIFamilyPath[]   { "/System/Library/Extensions/IOPCIFamily.kext/IOPCIFamily" };
static const char *kextAppleRTCPath[]      { "/System/Library/Extensions/AppleRTC.kext/Contents/MacOS/AppleRTC" };
static const char *kextX86PlatformPlugin[] { "/System/Library/Extensions/IOPlatformPluginFamily.kext/Contents/PlugIns/X86PlatformPlugin.kext/Contents/MacOS/X86PlatformPlugin" };

static KernelPatcher::KextInfo kextList[] {
	{"com.apple.iokit.IOPCIFamily",        kextIOPCIFamilyPath,   arrsize(kextIOPCIFamilyPath),   {true}, {}, KernelPatcher::KextInfo::Unloaded},
	{"com.apple.driver.AppleRTC",          kextAppleRTCPath,      arrsize(kextAppleRTCPath),      {true}, {}, KernelPatcher::KextInfo::Unloaded},
	{"com.apple.driver.X86PlatformPlugin", kextX86PlatformPlugin, arrsize(kextX86PlatformPlugin), {true}, {}, KernelPatcher::KextInfo::Unloaded},
};

//==============================================================================

bool HBFX::init()
{
	callbackHBFX = this;
	emulatedNVRAM = emuVariableIsDetected();

	lilu.onPatcherLoadForce(
	[](void *user, KernelPatcher &patcher) {
		callbackHBFX->processKernel(patcher);
	}, this);

	lilu.onKextLoadForce(kextList, arrsize(kextList),
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
	DBGLOG("HBFX", "IOHibernateSystemSleep is called, result is: 0x%x", result);

	uint32_t ioHibernateState = kIOHibernateStateInactive;
	if (WIOKit::getOSDataValue(IOService::getPMRootDomain(), kIOHibernateStateKey, ioHibernateState))
		DBGLOG("HBFX", "Current hibernate state from IOPMRootDomain is: %d", ioHibernateState);

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

			if (!callbackHBFX->nvstorage.save(FILE_NVRAM_NAME))
				callbackHBFX->nvstorage.save(BACKUP_FILE_NVRAM_NAME);

			if (callbackHBFX->sync)
				callbackHBFX->sync(kernproc, nullptr, nullptr);

			callbackHBFX->nvstorage.remove(kBoot0082Key);
			callbackHBFX->nvstorage.remove(kBootNextKey);
		}
	}

	return result;
}

//==============================================================================

IOReturn HBFX::IOHibernateSystemWake(void)
{
	callbackHBFX->sleepServiceWake    = false;
	callbackHBFX->wakeCalendarSet     = false;
	callbackHBFX->latestHibernateMode = 0;
	callbackHBFX->latestStandbyDelay  = 0;
	callbackHBFX->latestPoweroffDelay = 0;
	callbackHBFX->sleepPhase          = -1;
	callbackHBFX->sleepFactors        = 0;
	callbackHBFX->sleepReason         = 0;
	callbackHBFX->sleepType           = 0;
	callbackHBFX->sleepFlags          = 0;
	
	IOReturn result = FunctionCast(IOHibernateSystemWake, callbackHBFX->orgIOHibernateSystemWake)();
	DBGLOG("HBFX", "IOHibernateSystemWake is called, result is: 0x%x", result);
	
	OSString * wakeType = OSDynamicCast(OSString, IOService::getPMRootDomain()->getProperty(kIOPMRootDomainWakeTypeKey));
	OSString * wakeReason = OSDynamicCast(OSString, IOService::getPMRootDomain()->getProperty(kIOPMRootDomainWakeReasonKey));

	if (result == KERN_SUCCESS)
	{
		DBGLOG("HBFX", "IOHibernateSystemWake: wake type is: %s", wakeType ? wakeType->getCStringNoCopy() : "null");
		DBGLOG("HBFX", "IOHibernateSystemWake: wake reason is: %s", wakeReason ? wakeReason->getCStringNoCopy() : "null");
	}

	if (callbackHBFX->nextSleepTimer)
		callbackHBFX->nextSleepTimer->cancelTimeout();

	if (result == KERN_SUCCESS && wakeType && wakeReason)
	{
		if (strstr(wakeReason->getCStringNoCopy(), "RTC", strlen("RTC")) &&
			(!strcmp(wakeType->getCStringNoCopy(), kIOPMRootDomainWakeTypeSleepService) || !strcmp(wakeType->getCStringNoCopy(), kIOPMRootDomainWakeTypeMaintenance))
			)
		{
			callbackHBFX->sleepServiceWake = true;
			DBGLOG("HBFX", "IOHibernateSystemWake: Maintenance/SleepService wake");
			uint32_t delta_time = 0;
			bool pmset_default_mode = false;
			if (callbackHBFX->isStandbyEnabled(delta_time, pmset_default_mode) && pmset_default_mode && callbackHBFX->nextSleepTimer)
				callbackHBFX->nextSleepTimer->setTimeoutMS(20000);
		}
	}
	
	return result;
}

//==============================================================================

void HBFX::IOPMrootDomain_willEnterFullWake(IOPMrootDomain* that)
{
	DBGLOG("HBFX", "willEnterFullWake called");
	FunctionCast(IOPMrootDomain_willEnterFullWake, callbackHBFX->orgIOPMrootDomain_willEnterFullWake)(that);

	callbackHBFX->sleepServiceWake    = false;
	callbackHBFX->wakeCalendarSet     = false;
	callbackHBFX->latestHibernateMode = 0;
	callbackHBFX->latestStandbyDelay  = 0;
	callbackHBFX->latestPoweroffDelay = 0;
	callbackHBFX->sleepPhase          = -1;
	callbackHBFX->sleepFactors        = 0;
	callbackHBFX->sleepReason         = 0;
	callbackHBFX->sleepType           = 0;
	callbackHBFX->sleepFlags          = 0;

	if (callbackHBFX->nextSleepTimer)
		callbackHBFX->nextSleepTimer->cancelTimeout();
}

//==============================================================================

IOReturn HBFX::IOPMrootDomain_setMaintenanceWakeCalendar(IOPMrootDomain* that, IOPMCalendarStruct * calendar)
{
	DBGLOG("HBFX", "Calendar time %02d.%02d.%04d %02d:%02d:%02d, selector: %d", calendar->day, calendar->month, calendar->year,
		   calendar->hour, calendar->minute, calendar->second, calendar->selector);

	IOReturn result = KERN_SUCCESS;

	if (callbackHBFX->sleepServiceWake) {
		DBGLOG("HBFX", "setMaintenanceWakeCalendar called after sleepServiceWake is set");
		return result;
	}

	uint32_t delta_time = 0;
	bool pmset_default_mode = false;
	callbackHBFX->wakeCalendarSet = false;
	if (callbackHBFX->isStandbyEnabled(delta_time, pmset_default_mode) && pmset_default_mode && delta_time != 0)
	{
		struct tm tm;
		struct timeval tv;
		microtime(&tv);
		
		gmtime_r(tv.tv_sec, &tm);
		DBGLOG("HBFX", "Current time: %02d.%02d.%04d %02d:%02d:%02d", tm.tm_mday, tm.tm_mon, tm.tm_year, tm.tm_hour, tm.tm_min, tm.tm_sec);

        tv.tv_sec += delta_time;
		gmtime_r(tv.tv_sec, &tm);
		DBGLOG("HBFX", "Postpone maintenance wake to: %02d.%02d.%04d %02d:%02d:%02d", tm.tm_mday, tm.tm_mon, tm.tm_year, tm.tm_hour, tm.tm_min, tm.tm_sec);

		*calendar = { static_cast<UInt32>(tm.tm_year), static_cast<UInt8>(tm.tm_mon), static_cast<UInt8>(tm.tm_mday),
			static_cast<UInt8>(tm.tm_hour), static_cast<UInt8>(tm.tm_min), static_cast<UInt8>(tm.tm_sec), calendar->selector };
		result = FunctionCast(IOPMrootDomain_setMaintenanceWakeCalendar, callbackHBFX->orgIOPMrootDomain_setMaintenanceWakeCalendar)(that, calendar);
		callbackHBFX->wakeCalendarSet = (result == KERN_SUCCESS);
	}
	else
		result = FunctionCast(IOPMrootDomain_setMaintenanceWakeCalendar, callbackHBFX->orgIOPMrootDomain_setMaintenanceWakeCalendar)(that, calendar);

	return result;
}

//==============================================================================

IOReturn HBFX::AppleRTC_setupDateTimeAlarm(void *that, void* rctDateTime)
{
	DBGLOG("HBFX", "AppleRTC::setupDateTimeAlarm is called, set alarm to seconds: %lld", callbackHBFX->convertDateTimeToSeconds(rctDateTime));

	if (callbackHBFX->sleepServiceWake) {
		DBGLOG("HBFX", "AppleRTC::setupDateTimeAlarm called after sleepServiceWake is set");
		return KERN_SUCCESS;
	}
	
	IOPMrootDomain * pmRootDomain = reinterpret_cast<IOService*>(that)->getPMRootDomain();
	if (pmRootDomain) {
		uint32_t delta_time = 0;
		bool pmset_default_mode = false;
		if (callbackHBFX->isStandbyEnabled(delta_time, pmset_default_mode) && pmset_default_mode && delta_time != 0)
		{
			struct tm tm;
			struct timeval tv;
			microtime(&tv);
			
			gmtime_r(tv.tv_sec, &tm);
			DBGLOG("HBFX", "Current time: %02d.%02d.%04d %02d:%02d:%02d", tm.tm_mday, tm.tm_mon, tm.tm_year, tm.tm_hour, tm.tm_min, tm.tm_sec);

			tv.tv_sec += delta_time;
			gmtime_r(tv.tv_sec, &tm);
			DBGLOG("HBFX", "Postpone RTC wake to: %02d.%02d.%04d %02d:%02d:%02d", tm.tm_mday, tm.tm_mon, tm.tm_year, tm.tm_hour, tm.tm_min, tm.tm_sec);
			
			callbackHBFX->convertSecondsToDateTime(tv.tv_sec, rctDateTime);
		}
	}
	else
		SYSLOG("HBFX", "IOPMrootDomain cannot be obtained from AppleRTC");

	return FunctionCast(AppleRTC_setupDateTimeAlarm, callbackHBFX->orgAppleRTC_setupDateTimeAlarm)(that, rctDateTime);
}

//==============================================================================

IOReturn HBFX::X86PlatformPlugin_sleepPolicyHandler(void * target, IOPMSystemSleepPolicyVariables * vars, IOPMSystemSleepParameters * params)
{
	bool forceHibernate = false;
	
	IOReturn result = FunctionCast(X86PlatformPlugin_sleepPolicyHandler, callbackHBFX->orgX86PlatformPlugin_sleepPolicyHandler)(target, vars, params);
	if (result != KERN_SUCCESS)
		SYSLOG("HBFX", "orgSleepPolicyHandler returned error 0x%x", result);
	else {
		DBGLOG("HBFX", "X86PlatformPlugin_sleepPolicyHandler sleepReason: %d, sleepPhase: %d, hibernateMode: %d, sleepType: %d",
			   vars->sleepReason, vars->sleepPhase, vars->hibernateMode, params->sleepType);
		DBGLOG("HBFX", "X86PlatformPlugin_sleepPolicyHandler standbyDelay: %d, standbyTimer: %d, poweroffDelay: %d, poweroffTimer: %d",
			   vars->standbyDelay, vars->standbyTimer, vars->poweroffDelay, vars->poweroffTimer);
		DBGLOG("HBFX", "X86PlatformPlugin_sleepPolicyHandler ecWakeTimer: %d, ecPoweroffTimer: %d", params->ecWakeTimer, params->ecPoweroffTimer);
		callbackHBFX->latestHibernateMode = vars->hibernateMode;
		callbackHBFX->latestStandbyDelay  = vars->standbyDelay;
        callbackHBFX->latestPoweroffDelay = vars->poweroffDelay;
		callbackHBFX->sleepPhase          = vars->sleepPhase;
		if (vars->sleepPhase == kIOPMSleepPhase0)
		{
			callbackHBFX->sleepFactors = vars->sleepFactors;
			callbackHBFX->sleepReason  = vars->sleepReason;
			callbackHBFX->sleepType    = params->sleepType;
			callbackHBFX->sleepFlags   = params->sleepFlags;
		}
		
		if (callbackHBFX->nextSleepTimer)
			callbackHBFX->nextSleepTimer->cancelTimeout();

		uint32_t delta_time = 0;
		bool pmset_default_mode = false;
		auto autoHibernateMode = ADDPR(hbfx_config).autoHibernateMode;
		while (callbackHBFX->isStandbyEnabled(delta_time, pmset_default_mode) && (params->sleepType == kIOPMSleepTypeDeepIdle || params->sleepType == kIOPMSleepTypeStandby))
		{
			IOPMPowerSource *power_source = callbackHBFX->getPowerSource();
			if (power_source && power_source->batteryInstalled()) {
				bool whenExternalPowerIsDisconnected = (autoHibernateMode & Configuration::WhenExternalPowerIsDisconnected);
				bool whenBatteryIsNotCharging = (autoHibernateMode & Configuration::WhenBatteryIsNotCharging);
				bool whenBatteryIsAtWarnLevel = (autoHibernateMode & Configuration::WhenBatteryIsAtWarnLevel);
				bool whenBatteryAtCriticalLevel = (autoHibernateMode & Configuration::WhenBatteryAtCriticalLevel);
				int  minimalRemainingCapacity = ((autoHibernateMode & 0xF00) >> 8);

				if (whenExternalPowerIsDisconnected && power_source->externalConnected()) {
					DBGLOG("HBFX", "Auto hibernate: external is connected, do not force to hibernate");
					callbackHBFX->sleepServiceWake = false;
					if (pmset_default_mode && delta_time != 0 && !callbackHBFX->wakeCalendarSet && callbackHBFX->sleepPhase > kIOPMSleepPhase0)
						callbackHBFX->explicitlyCallSetMaintenanceWakeCalendar();
					break;
				}

				if (whenBatteryIsNotCharging && power_source->isCharging()) {
					DBGLOG("HBFX", "Auto hibernate: battery is charging, do not force to hibernate");
					callbackHBFX->sleepServiceWake = false;
					if (pmset_default_mode && delta_time != 0 && !callbackHBFX->wakeCalendarSet && callbackHBFX->sleepPhase > kIOPMSleepPhase0)
						callbackHBFX->explicitlyCallSetMaintenanceWakeCalendar();
					break;
				}

				if (!power_source->isCharging())
				{
					DBGLOG("HBFX", "Auto hibernate: warning level = %d, critical level = %d, capacity remaining = %d, minimal capacity = %d",
						   power_source->atWarnLevel(), power_source->atCriticalLevel(),
						   power_source->capacityPercentRemaining(), minimalRemainingCapacity);
					if (whenBatteryIsAtWarnLevel && power_source->atWarnLevel()) {
						DBGLOG("HBFX", "Auto hibernate: Battery is at warning level, capacity remaining: %d, force to hibernate", power_source->capacityPercentRemaining());
						forceHibernate = true;
					}

					if (whenBatteryAtCriticalLevel && power_source->atCriticalLevel()) {
						DBGLOG("HBFX", "Auto hibernate: battery is at critical level, capacity remaining: %d, force to hibernate", power_source->capacityPercentRemaining());
						forceHibernate = true;
					}

					if (!forceHibernate && (whenBatteryIsAtWarnLevel || whenBatteryAtCriticalLevel) && minimalRemainingCapacity != 0 &&
						power_source->capacityPercentRemaining() <= minimalRemainingCapacity)
					{
						DBGLOG("HBFX", "Auto hibernate: capacity remaining: %d less than minimal: %d, force to hibernate", power_source->capacityPercentRemaining(), minimalRemainingCapacity);
						forceHibernate = true;
					}
				}
			}

			bool whenLidIsClosed = (autoHibernateMode & Configuration::WhenLidIsClosed);
			bool lidIsOpen = OSDynamicCast(OSBoolean, IOService::getPMRootDomain()->getProperty(kAppleClamshellStateKey)) != kOSBooleanTrue;
			if (!forceHibernate && whenLidIsClosed && lidIsOpen) {
				DBGLOG("HBFX", "Auto hibernate: clamshell is open, do not force to hibernate");
				callbackHBFX->sleepServiceWake = false;
				if (pmset_default_mode && delta_time != 0 && !callbackHBFX->wakeCalendarSet && callbackHBFX->sleepPhase > kIOPMSleepPhase0)
					callbackHBFX->explicitlyCallSetMaintenanceWakeCalendar();
				break;
			}

			if (pmset_default_mode && delta_time != 0 && !forceHibernate && !callbackHBFX->wakeCalendarSet && !callbackHBFX->sleepServiceWake)
			{
				callbackHBFX->explicitlyCallSetMaintenanceWakeCalendar();
			}

			bool setupHibernate = (forceHibernate || !callbackHBFX->wakeCalendarSet || callbackHBFX->sleepServiceWake || (pmset_default_mode && delta_time == 0));
			if (callbackHBFX->sleepPhase < kIOPMSleepPhase2 && setupHibernate)
			{
				vars->sleepFactors = callbackHBFX->sleepFactors;
				vars->sleepReason  = callbackHBFX->sleepReason;
				params->sleepType  = kIOPMSleepTypeStandby;
				params->sleepFlags = kIOPMSleepFlagHibernate;
				DBGLOG("HBFX", "Auto hibernate: sleep phase %d, set hibernate values", callbackHBFX->sleepPhase);
			}
			else if (callbackHBFX->sleepPhase == kIOPMSleepPhase2)
			{
				if (setupHibernate)
				{
					vars->sleepFactors = callbackHBFX->sleepFactors;
					vars->sleepReason  = callbackHBFX->sleepReason;
					params->sleepType  = kIOPMSleepTypeHibernate;
					params->sleepFlags = kIOPMSleepFlagHibernate;
					DBGLOG("HBFX", "Auto hibernate: sleep phase %d, hibernate now", callbackHBFX->sleepPhase);
				}
				else
				{
					vars->sleepFactors = callbackHBFX->sleepFactors;
					vars->sleepReason  = callbackHBFX->sleepReason;
					params->sleepType  = callbackHBFX->sleepType;
					params->sleepFlags = callbackHBFX->sleepFlags;
					DBGLOG("HBFX", "Auto hibernate: sleep phase %d, postpone hibernate", callbackHBFX->sleepPhase);
				}
				
				callbackHBFX->sleepServiceWake = false;
				callbackHBFX->wakeCalendarSet  = false;
			}
			else if (forceHibernate)
			{
				DBGLOG("HBFX", "Auto hibernate: force hibernate...");
			}
			break;
		}
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

IOReturn HBFX::IOPCIBridge_restoreMachineState(IOService *that, IOOptionBits options, IOService * device)
{
	if (kMachineRestoreDehibernate & options)
		callbackHBFX->correct_pci_config_command = true;

	IOReturn result = FunctionCast(IOPCIBridge_restoreMachineState, callbackHBFX->orgIOPCIBridge_restoreMachineState)(that, options, device);
	DBGLOG("HBFX", "restoreMachineState returned 0x%x for device %s, options = 0x%x", result, that->getName(), options);

	if (kMachineRestoreDehibernate & options)
		callbackHBFX->correct_pci_config_command = false;
	
	return result;
}

//==============================================================================

void HBFX::IOPCIDevice_extendedConfigWrite16(IOService *that, UInt64 offset, UInt16 data)
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

	FunctionCast(IOPCIDevice_extendedConfigWrite16, callbackHBFX->orgIOPCIDevice_extendedConfigWrite16)(that, offset, data);
}

//==============================================================================

void HBFX::processKernel(KernelPatcher &patcher)
{
	if (!(progressState & ProcessingState::KernelRouted))
	{
		bool auto_hibernate_mode_on = (ADDPR(hbfx_config).autoHibernateMode & Configuration::EnableAutoHibernation);
		if (!ADDPR(hbfx_config).dumpNvram && emulatedNVRAM)
			ADDPR(hbfx_config).dumpNvram = true;
		bool nvram_patches_required = (ADDPR(hbfx_config).dumpNvram == true || !checkRTCExtendedMemory());
		if (!nvram_patches_required)
		{
			DBGLOG("HBFX", "all nvram kernel patches will be skipped since the second bank of RTC memory is available");
			if (!auto_hibernate_mode_on)
				return;
		}

		if (auto_hibernate_mode_on) {
			KernelPatcher::RouteRequest requests[] = {
				{"__ZN14IOPMrootDomain17willEnterFullWakeEv", IOPMrootDomain_willEnterFullWake, orgIOPMrootDomain_willEnterFullWake},
				{"__ZN14IOPMrootDomain26setMaintenanceWakeCalendarEPK18IOPMCalendarStruct", IOPMrootDomain_setMaintenanceWakeCalendar, orgIOPMrootDomain_setMaintenanceWakeCalendar},
				{"_IOHibernateSystemWake", IOHibernateSystemWake, orgIOHibernateSystemWake}
			};
			if (!patcher.routeMultiple(KernelPatcher::KernelID, requests, arrsize(requests))) {
				SYSLOG("HBFX", "patcher.routeMultiple for %s is failed with error %d", requests[0].symbol, patcher.getError());
				patcher.clearError();
			}
			else if (!nextSleepTimer) {
				if (!workLoop)
					workLoop = IOWorkLoop::workLoop();
					
				if (workLoop) {
					nextSleepTimer = IOTimerEventSource::timerEventSource(workLoop,
					[](OSObject *owner, IOTimerEventSource *sender) {
						DBGLOG("HBFX", "Force system to sleep by calling IOPMrootDomain::receivePowerNotification");
						IOReturn result = IOService::getPMRootDomain()->receivePowerNotification(kIOPMSleepNow);
						if (result != KERN_SUCCESS)
							SYSLOG("HBFX", "IOPMrootDomain::receivePowerNotification failed with error 0x%x", result);
						if (sender)
							sender->setTimeoutMS(20000);
					});
					
					if (nextSleepTimer) {
						IOReturn result = workLoop->addEventSource(nextSleepTimer);
						if (result != kIOReturnSuccess)
							SYSLOG("HBFX", "addEventSource failed");
					}
					else
						SYSLOG("HBFX", "timerEventSource failed");
				}
				else
					SYSLOG("HBFX", "IOService instance does not have workLoop");
			}
		}

		if (nvram_patches_required && initialize_nvstorage()) {
			KernelPatcher::RouteRequest request	{"_IOHibernateSystemSleep", IOHibernateSystemSleep, orgIOHibernateSystemSleep};
			if (!patcher.routeMultiple(KernelPatcher::KernelID, &request, 1)) {
				SYSLOG("HBFX", "patcher.routeMultiple for %s is failed with error %d", request.symbol, patcher.getError());
				patcher.clearError();
			}

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
					if (!patcher.routeMultiple(KernelPatcher::KernelID, &request, 1))
						SYSLOG("HBFX", "patcher.routeMultiple for %s is failed with error %d", request.symbol, patcher.getError());
					patcher.clearError();
				}
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
	if (progressState != ProcessingState::EverythingDone)
	{
		if (!ADDPR(hbfx_config).dumpNvram && emulatedNVRAM)
			ADDPR(hbfx_config).dumpNvram = true;
		bool nvram_patches_required = (ADDPR(hbfx_config).dumpNvram == true || !checkRTCExtendedMemory());
		if (nvram_patches_required)
			initialize_nvstorage();

		bool auto_hibernate_mode_on = (ADDPR(hbfx_config).autoHibernateMode & Configuration::EnableAutoHibernation);
		
		for (size_t i = 0; i < arrsize(kextList); i++)
		{
			if (kextList[i].loadIndex == index)
			{
				if (ADDPR(hbfx_config).patchPCIFamily && i == 0 && !(progressState & ProcessingState::IOPCIFamilyRouted))
				{
					KernelPatcher::RouteRequest requests[] {
						{"__ZN11IOPCIBridge19restoreMachineStateEjP11IOPCIDevice", IOPCIBridge_restoreMachineState, orgIOPCIBridge_restoreMachineState},
						{"__ZN11IOPCIDevice21extendedConfigWrite16Eyt", IOPCIDevice_extendedConfigWrite16, orgIOPCIDevice_extendedConfigWrite16},
					};

					if (!patcher.routeMultiple(index, requests, address, size))
						SYSLOG("HBFX", "patcher.routeMultiple for %s is failed with error %d", requests[0].symbol, patcher.getError());
					patcher.clearError();
					progressState |= ProcessingState::IOPCIFamilyRouted;
				}
				
				if (auto_hibernate_mode_on && i == 1 && !(progressState & ProcessingState::AppleRTCRouted))
				{
					convertDateTimeToSeconds = reinterpret_cast<t_convertDateTimeToSeconds>(patcher.solveSymbol(index, "__ZL24convertDateTimeToSecondsPK11RTCDateTime"));
					if (!convertDateTimeToSeconds) {
						SYSLOG("HBFX", "failed to resolve __ZL24convertDateTimeToSecondsPK11RTCDateTime %d", patcher.getError());
						patcher.clearError();
					}

					convertSecondsToDateTime = reinterpret_cast<t_convertSecondsToDateTime>(patcher.solveSymbol(index, "__ZL24convertSecondsToDateTimelP11RTCDateTime"));
					if (!convertSecondsToDateTime) {
						SYSLOG("HBFX", "failed to resolve __ZL24convertSecondsToDateTimelP11RTCDateTime %d", patcher.getError());
						patcher.clearError();
					}
					
					if (convertDateTimeToSeconds && convertSecondsToDateTime) {
						KernelPatcher::RouteRequest request
							{"__ZN8AppleRTC18setupDateTimeAlarmEPK11RTCDateTime", AppleRTC_setupDateTimeAlarm, orgAppleRTC_setupDateTimeAlarm};
						
						if (!patcher.routeMultiple(index, &request, 1, address, size))
							SYSLOG("HBFX", "patcher.routeMultiple for %s is failed with error %d", request.symbol, patcher.getError());
						patcher.clearError();
					}
					progressState |= ProcessingState::AppleRTCRouted;
				}
				
				if (auto_hibernate_mode_on && i == 2 && !(progressState & ProcessingState::X86PluginRouted))
				{
					KernelPatcher::RouteRequest request
						{"__ZN17X86PlatformPlugin18sleepPolicyHandlerEPK30IOPMSystemSleepPolicyVariablesP25IOPMSystemSleepParameters", X86PlatformPlugin_sleepPolicyHandler, orgX86PlatformPlugin_sleepPolicyHandler};
					
					if (!patcher.routeMultiple(index, &request, 1, address, size))
						SYSLOG("HBFX", "patcher.routeMultiple for %s is failed with error %d", request.symbol, patcher.getError());
					patcher.clearError();
					progressState |= ProcessingState::X86PluginRouted;
				}
		    }
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

//==============================================================================

bool HBFX::emuVariableIsDetected()
{
	bool result = false;
	IORegistryEntry *reg_entry = nullptr;
	
	if (gIODTPlane != nullptr && (reg_entry = IORegistryEntry::fromPath("/options", gIODTPlane)) != nullptr)
	{
		auto reg_variable  = OSDynamicCast(OSData, reg_entry->getProperty("EmuVariableUefiPresent"));
		bool emu_detected  = (reg_variable != nullptr && reg_variable->isEqualTo("Yes", 3));
		reg_entry->release();
		if (emu_detected)
		{
			DBGLOG("HBFX", "EmuVariableUefiPresent is detected");
			result = true;
		}
		else
		{
			DBGLOG("HBFX", "EmuVariableUefiPresent is not detected");
		}
	}
	else
	{
		auto rt = EfiRuntimeServices::get(true);
		if (rt) {
			uint64_t size = 5;
			auto buf = Buffer::create<uint8_t>(size);
			if (buf) {
				uint32_t attr = 0;
				static constexpr EFI_GUID AppleBootGuid { 0x7C436110, 0xAB2A, 0x4BBB, { 0xA8, 0x80, 0xFE, 0x41, 0x99, 0x5C, 0x9F, 0x82 } };
				auto status = rt->getVariable(u"EmuVariableUefiPresent", &AppleBootGuid, &attr, &size, buf);
				if (status == EFI_SUCCESS) {
					result = (size >= 3 && buf[0] == 'Y' && buf[1] == 'e' && buf[2] == 's');
				}
				else if (status != EFI_NOT_FOUND)
				{
					DBGLOG("HBFX", "Failed to read efi rt services for EmuVariableUefiPresent, error code: 0x%x", status);
				}
				
				DBGLOG("HBFX", "EmuVariableUefiPresent is %s", result ? "detected" : "not detected");
				Buffer::deleter(buf);
			}
			else
				SYSLOG("HBFX", "failed to create temporary buffer");
			rt->put();
		}
		else
			SYSLOG("HBFX", "failed to load efi rt services for rtc-blacklist");
	}
	
	return result;
}

//==============================================================================

IOPMPowerSource *HBFX::getPowerSource()
{
	static int attempt_count = 5;
	static IOPMPowerSource *power_source {nullptr};
	if (power_source == nullptr && --attempt_count >= 0)
	{
		auto matching = IOService::serviceMatching("IOPMPowerSource");
		if (matching) {
			auto service = IOService::waitForMatchingService(matching, 300);
			if (service) {
				power_source = OSDynamicCast(IOPMPowerSource, service);
				if (!power_source) {
					SYSLOG("HBFX", "failed to cast service object to IOPMPowerSource");
					OSSafeReleaseNULL(service);
				}
			}
			else
				SYSLOG("HBFX", "failed to get service object for IOPMPowerSource");
			matching->release();
		} else {
			SYSLOG("HBFX", "failed to allocate IOPMPowerSource service matching");
		}
	}
	
	return power_source;
}

//==============================================================================

bool HBFX::isStandbyEnabled(uint32_t &delta_time, bool &pmset_default_mode)
{
	bool deepSleepEnabled = OSDynamicCast(OSBoolean, IOService::getPMRootDomain()->getProperty(kIOPMDeepSleepEnabledKey)) == kOSBooleanTrue;
	bool autoPowerOffEnabled = OSDynamicCast(OSBoolean, IOService::getPMRootDomain()->getProperty(kIOPMAutoPowerOffEnabledKey)) == kOSBooleanTrue;
	bool standbyEnabled = deepSleepEnabled || autoPowerOffEnabled;

	if (deepSleepEnabled)
		delta_time = callbackHBFX->latestStandbyDelay;
	else if (autoPowerOffEnabled)
		delta_time = callbackHBFX->latestPoweroffDelay;
	else
		delta_time = 0;
	
	pmset_default_mode = (callbackHBFX->latestHibernateMode == (kIOHibernateModeOn | kIOHibernateModeSleep));
	return standbyEnabled;
}

//==============================================================================

IOReturn HBFX::explicitlyCallSetMaintenanceWakeCalendar()
{
	struct tm tm;
	struct timeval tv;
	microtime(&tv);
	gmtime_r(tv.tv_sec, &tm);
	IOPMCalendarStruct calendar {(UInt32)tm.tm_year, (UInt8)tm.tm_mon, (UInt8)tm.tm_mday, (UInt8)tm.tm_hour, (UInt8)tm.tm_min, (UInt8)tm.tm_sec, (UInt8)kPMCalendarTypeMaintenance};
	DBGLOG("HBFX", "call setMaintenanceWakeCalendar explicitly");
	return IOPMrootDomain_setMaintenanceWakeCalendar(IOService::getPMRootDomain(), &calendar);
}
