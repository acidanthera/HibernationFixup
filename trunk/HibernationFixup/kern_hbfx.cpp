//
//  kern_hbfx.cpp
//  HBFX
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#include <Library/LegacyIOService.h>
#include "LegacyRootDomain.h"

#include <Headers/kern_api.hpp>
#include <Headers/kern_file.hpp>
#include <Headers/kern_compat.hpp>
#include <Headers/kern_compression.hpp>

#include "kern_config.hpp"
#include "kern_hbfx.hpp"

// Only used in apple-driven callbacks
static HBFX *callbackHBFX = nullptr;
static KernelPatcher *callbackPatcher = nullptr;

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

/* Definitions of PCI Config Registers */
enum {
    kIOPCIConfigVendorID                = 0x00,
    kIOPCIConfigDeviceID                = 0x02,
    kIOPCIConfigCommand                 = 0x04,
    kIOPCIConfigStatus                  = 0x06,
    kIOPCIConfigRevisionID              = 0x08,
    kIOPCIConfigClassCode               = 0x09,
    kIOPCIConfigCacheLineSize           = 0x0C,
    kIOPCIConfigLatencyTimer            = 0x0D,
    kIOPCIConfigHeaderType              = 0x0E,
    kIOPCIConfigBIST                    = 0x0F,
    kIOPCIConfigBaseAddress0            = 0x10,
    kIOPCIConfigBaseAddress1            = 0x14,
    kIOPCIConfigBaseAddress2            = 0x18,
    kIOPCIConfigBaseAddress3            = 0x1C,
    kIOPCIConfigBaseAddress4            = 0x20,
    kIOPCIConfigBaseAddress5            = 0x24,
    kIOPCIConfigCardBusCISPtr           = 0x28,
    kIOPCIConfigSubSystemVendorID       = 0x2C,
    kIOPCIConfigSubSystemID             = 0x2E,
    kIOPCIConfigExpansionROMBase        = 0x30,
    kIOPCIConfigCapabilitiesPtr         = 0x34,
    kIOPCIConfigInterruptLine           = 0x3C,
    kIOPCIConfigInterruptPin            = 0x3D,
    kIOPCIConfigMinimumGrant            = 0x3E,
    kIOPCIConfigMaximumLatency          = 0x3F
};


static const char *kextIOPCIFamilyPath[] { "/System/Library/Extensions/IOPCIFamily.kext/IOPCIFamily" };

static KernelPatcher::KextInfo kextList[] {
    { "com.apple.iokit.IOPCIFamily", kextIOPCIFamilyPath, 1, {true}, {}, KernelPatcher::KextInfo::Unloaded }
};

static const size_t kextListSize {arrsize(kextList)};


//==============================================================================

bool HBFX::init()
{
    LiluAPI::Error error = lilu.onPatcherLoad(
                                              [](void *user, KernelPatcher &patcher) {
                                                  callbackHBFX = static_cast<HBFX *>(user);
                                                  callbackPatcher = &patcher;
                                                  callbackHBFX->processKernel(patcher);
                                              }, this);
    
    if (error != LiluAPI::Error::NoError) {
        SYSLOG("HBFX", "failed to register onPatcherLoad method %d", error);
        return false;
    }
    
    error = lilu.onKextLoad(kextList, kextListSize,
                                           [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
                                               callbackHBFX = static_cast<HBFX *>(user);
                                               callbackPatcher = &patcher;
                                               callbackHBFX->processKext(patcher, index, address, size);
                                           }, this);
    
    if (error != LiluAPI::Error::NoError) {
        SYSLOG("HBFX", "failed to register onKextLoad method %d", error);
        return false;
    }

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
    IOReturn result = KERN_SUCCESS;
    
    if (callbackHBFX && callbackHBFX->orgIOHibernateSystemSleep)
    {
        result = callbackHBFX->orgIOHibernateSystemSleep();
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
                else
                {
                    SYSLOG("HBFX", "IOHibernateRTCVariablesKey has been written to NVRAM.");
                    
                    // we should remove fakesmc-key-HBKP-ch8* if it exists
                    if (callbackHBFX->nvstorage.exists(kFakeSMCHBKB))
                    {
                        callbackHBFX->nvstorage.remove(kFakeSMCHBKB);
                        SYSLOG("HBFX", "fakesmc-key-HBKP-ch8* has been removed from NVRAM.");
                    }
                }
            }
            
            OSData *smc = OSDynamicCast(OSData, IOService::getPMRootDomain()->getProperty(kIOHibernateSMCVariablesKey));
            if (smc && !callbackHBFX->nvstorage.exists(kIOHibernateSMCVariablesKey))
            {
                if (!callbackHBFX->nvstorage.write(kIOHibernateSMCVariablesKey, smc, NVStorage::OptRaw))
                    SYSLOG("HBFX", "IOHibernateSMCVariablesKey can't be written to NVRAM.");
            }
            
            if (config.dumpNvram)
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
    }
    else {
        SYSLOG("HBFX", "callback arrived at nowhere");
    }

    return result;
}

//==============================================================================

int HBFX::packA(char *inbuf, uint32_t length, uint32_t buflen)
{
    char key[128];
    unsigned int bufpos = 0;
    if (callbackHBFX && callbackHBFX->orgPackA)
        bufpos = callbackHBFX->orgPackA(inbuf, length, buflen);
    
    if (callbackHBFX && !callbackHBFX->ml_at_interrupt_context())
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
    IOReturn result = KERN_SUCCESS;
    if (callbackHBFX && callbackHBFX->orgRestoreMachineState)
    {
        if (kMachineRestoreDehibernate & options)
            callbackHBFX->disable_pci_config_command = true;
        
        result = callbackHBFX->orgRestoreMachineState(that, options, device);
        
        if (kMachineRestoreDehibernate & options)
            callbackHBFX->disable_pci_config_command = false;
    }
    
    return result;
}

//==============================================================================

void HBFX::extendedConfigWrite16(IOService *that, UInt64 offset, UInt16 data)
{
    if (callbackHBFX && callbackHBFX->orgExtendedConfigWrite16)
    {
        if (callbackHBFX->disable_pci_config_command && offset == kIOPCIConfigCommand)
        {
            if (strlen(config.ignored_device_list) == 0 || strstr(config.ignored_device_list, that->getName()) != nullptr)
            {
                DBGLOG("HBFX", "extendedConfigWrite16 won't be called for device %s", that->getName());
                return;
            }
        }
        
        callbackHBFX->orgExtendedConfigWrite16(that, offset, data);
    }
}

//==============================================================================

void HBFX::processKernel(KernelPatcher &patcher)
{
    if (!initialize_nvstorage())
        return;
    
    if (!(progressState & ProcessingState::KernelRouted))
    {
        auto method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_IOHibernateSystemSleep");
        if (method_address) {
            DBGLOG("HBFX", "obtained _IOHibernateSystemSleep");
            orgIOHibernateSystemSleep = reinterpret_cast<t_io_hibernate_system_sleep_callback>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOHibernateSystemSleep), true));
            if (patcher.getError() == KernelPatcher::Error::NoError) {
                DBGLOG("HBFX", "routed _IOHibernateSystemSleep");
            } else {
                SYSLOG("HBFX", "failed to route _IOHibernateSystemSleep");
            }
        } else {
            SYSLOG("HBFX", "failed to resolve _IOHibernateSystemSleep");
        }
        
        method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_ml_at_interrupt_context");
        if (method_address) {
            DBGLOG("HBFX", "obtained _ml_at_interrupt_context");
            ml_at_interrupt_context = reinterpret_cast<t_ml_at_interrupt_context>(method_address);
        } else {
            SYSLOG("HBFX", "failed to resolve _ml_at_interrupt_context");
        }
        
        method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_ml_get_interrupts_enabled");
        if (method_address) {
            DBGLOG("HBFX", "obtained _ml_get_interrupts_enabled");
            ml_get_interrupts_enabled = reinterpret_cast<t_ml_get_interrupts_enabled>(method_address);
        } else {
            SYSLOG("HBFX", "failed to resolve _ml_get_interrupts_enabled");
        }
        
        method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_ml_set_interrupts_enabled");
        if (method_address) {
            DBGLOG("HBFX", "obtained _ml_set_interrupts_enabled");
            ml_set_interrupts_enabled = reinterpret_cast<t_ml_set_interrupts_enabled>(method_address);
        } else {
            SYSLOG("HBFX", "failed to resolve _ml_set_interrupts_enabled");
        }
        
        if (config.dumpNvram)
        {
            method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_sync");
            if (method_address) {
                DBGLOG("HBFX", "obtained _sync");
                sync = reinterpret_cast<t_sync>(method_address);
            } else {
                SYSLOG("HBFX", "failed to resolve _sync");
            }
            
            method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_preemption_enabled");
            if (method_address) {
                DBGLOG("HBFX", "obtained _preemption_enabled");
                preemption_enabled = reinterpret_cast<t_preemption_enabled>(method_address);
            } else {
                SYSLOG("HBFX", "failed to resolve _preemption_enabled");
            }
            
            method_address = patcher.solveSymbol(KernelPatcher::KernelID, "__enable_preemption");
            if (method_address) {
                DBGLOG("HBFX", "obtained __enable_preemption");
                enable_preemption = reinterpret_cast<t_enable_preemption>(method_address);
            } else {
                SYSLOG("HBFX", "failed to resolve __enable_preemption");
            }
            
            method_address = patcher.solveSymbol(KernelPatcher::KernelID, "__disable_preemption");
            if (method_address) {
                DBGLOG("HBFX", "obtained __disable_preemption");
                disable_preemption = reinterpret_cast<t_disable_preemption>(method_address);
            } else {
                SYSLOG("HBFX", "failed to resolve __disable_preemption");
            }
            
            if (sync && preemption_enabled && enable_preemption && disable_preemption && ml_at_interrupt_context &&
                ml_get_interrupts_enabled && ml_set_interrupts_enabled)
            {
                method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_packA");
                if (method_address) {
                    DBGLOG("HBFX", "obtained _packA");
                    orgPackA = reinterpret_cast<t_pack_a>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(packA), true));
                    if (patcher.getError() == KernelPatcher::Error::NoError) {
                        DBGLOG("HBFX", "routed _packA");
                    } else {
                        SYSLOG("HBFX", "failed to route _packA");
                    }
                } else {
                    SYSLOG("HBFX", "failed to resolve _packA");
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
    if (!initialize_nvstorage())
        return;
    
    if (progressState != ProcessingState::EverythingDone)
    {
        if (config.patchPCIFamily)
        {
            for (size_t i = 0; i < kextListSize; i++)
            {
                if (kextList[i].loadIndex == index)
                {
                    if (!(progressState & ProcessingState::IOPCIFamilyRouted) && !strcmp(kextList[i].id, "com.apple.iokit.IOPCIFamily"))
                    {
                        auto method_address = patcher.solveSymbol(index, "__ZN11IOPCIBridge19restoreMachineStateEjP11IOPCIDevice");
                        if (method_address) {
                            DBGLOG("HBFX", "obtained __ZN11IOPCIBridge19restoreMachineStateEjP11IOPCIDevice");
                            orgRestoreMachineState = reinterpret_cast<t_restore_machine_state>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(restoreMachineState), true));
                            if (patcher.getError() == KernelPatcher::Error::NoError) {
                                DBGLOG("HBFX", "routed __ZN11IOPCIBridge19restoreMachineStateEjP11IOPCIDevice");
                            } else {
                                SYSLOG("HBFX", "failed to route __ZN11IOPCIBridge19restoreMachineStateEjP11IOPCIDevice");
                            }
                        } else {
                            SYSLOG("HBFX", "failed to resolve __ZN11IOPCIBridge19restoreMachineStateEjP11IOPCIDevice");
                        }
                        
                        if (orgRestoreMachineState)
                        {
                            method_address = patcher.solveSymbol(index, "__ZN11IOPCIDevice21extendedConfigWrite16Eyt");
                            if (method_address) {
                                DBGLOG("HBFX", "obtained __ZN11IOPCIDevice21extendedConfigWrite16Eyt");
                                orgExtendedConfigWrite16 = reinterpret_cast<t_extended_config_write16>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(extendedConfigWrite16), true));
                                if (patcher.getError() == KernelPatcher::Error::NoError) {
                                    DBGLOG("HBFX", "routed __ZN11IOPCIDevice21extendedConfigWrite16Eyt");
                                } else {
                                    SYSLOG("HBFX", "failed to route __ZN11IOPCIDevice21extendedConfigWrite16Eyt");
                                }
                            } else {
                                SYSLOG("HBFX", "failed to resolve __ZN11IOPCIDevice21extendedConfigWrite16Eyt");
                            }
                        }
                        
                        progressState |= ProcessingState::IOPCIFamilyRouted;
                    }
                }
            }
        }
    }
    
    // Ignore all the errors for other processors
    patcher.clearError();
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
            
            if (!config.dumpNvram)
            {
                OSData *data = nvstorage.read("EmuVariableUefiPresent", NVStorage::OptRaw);
                if (data && data->isEqualTo(OSString::withCStringNoCopy("Yes")))
                {
                    DBGLOG("HBFX", "EmuVariableUefiPresent is detected, set dumpNvram to true");
                    config.dumpNvram = true;
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
