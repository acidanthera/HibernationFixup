//
//  kern_hbfx.cpp
//  HBFX
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#include <Headers/kern_api.hpp>
#include <Headers/kern_file.hpp>
#include <Headers/kern_compat.hpp>

#include "kern_config.hpp"
#include "kern_hbfx.hpp"

#include <IOKit/IORegistryEntry.h>
#include <IOKit/IOReportTypes.h>

#include <IOKit/pwr_mgt/RootDomain.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IONVRAM.h>



// Only used in apple-driven callbacks
static HBFX *callbackHBFX = nullptr;
static KernelPatcher *callbackPatcher = nullptr;
static const OSSymbol *gIOHibernateRTCVariablesKey = nullptr;
static const OSSymbol *gIOHibernateSMCVariables = nullptr;
static const OSSymbol *gIOHibernateBoot0082Key = nullptr;
static const OSSymbol *gIOHibernateBootNextKey = nullptr;
static const OSSymbol *gBoot0082Key  = nullptr;
static const OSSymbol *gBootNextKey  = nullptr;
static const OSSymbol *gFakeSMCHBKP  = nullptr;

extern proc_t kernproc;


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

enum
{
    kIOPolledPreflightState           = 1,
    kIOPolledBeforeSleepState         = 2,
    kIOPolledAfterSleepState          = 3,
    kIOPolledPostflightState          = 4,
    
    kIOPolledPreflightCoreDumpState   = 5,
    kIOPolledPostflightCoreDumpState  = 6,
    
    kIOPolledBeforeSleepStateAborted  = 7,
};

static const char *kextIOPCIFamilyPath[] { "/System/Library/Extensions/IOPCIFamily.kext/IOPCIFamily" };

static KernelPatcher::KextInfo kextList[] {
    { "com.apple.iokit.IOPCIFamily", kextIOPCIFamilyPath, 1, {true, false}, {}, KernelPatcher::KextInfo::Unloaded }
};

static const size_t kextListSize {1};


//==============================================================================

bool HBFX::init()
{
    if (config.patchPCIFamily)
    {
        LiluAPI::Error error = lilu.onKextLoad(kextList, kextListSize,
                                               [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
                                                   callbackHBFX = static_cast<HBFX *>(user);
                                                   callbackPatcher = &patcher;
                                                   callbackHBFX->processKext(patcher, index, address, size);
                                               }, this);
        
        if (error != LiluAPI::Error::NoError) {
            SYSLOG("HBFX", "failed to register onKextLoad method %d", error);
            return false;
        }
    }
    else
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
    }
    
    gIOHibernateRTCVariablesKey = OSSymbol::withCStringNoCopy(kIOHibernateRTCVariablesKey);
    gIOHibernateSMCVariables    = OSSymbol::withCStringNoCopy(kIOHibernateSMCVariablesKey);
    gIOHibernateBoot0082Key     = OSSymbol::withCString("8BE4DF61-93CA-11D2-AA0D-00E098032B8C:Boot0082");
    gIOHibernateBootNextKey     = OSSymbol::withCString("8BE4DF61-93CA-11D2-AA0D-00E098032B8C:BootNext");
    gBoot0082Key                = OSSymbol::withCString("Boot0082");
    gBootNextKey                = OSSymbol::withCString("BootNext");
    gFakeSMCHBKP                = OSSymbol::withCStringNoCopy(kFakeSMCHBKB);
    
	return true;
}

//==============================================================================

void HBFX::deinit()
{
}

//==============================================================================

IOReturn HBFX::IOHibernateSystemSleep(void)
{
    IOReturn result = KERN_SUCCESS;
    
    if (callbackHBFX && callbackHBFX->orgIOHibernateSystemSleep && callbackHBFX->dtNvram)
    {
        callbackHBFX->file_vars_valid = false;
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
            if (callbackHBFX->file_vars_valid && callbackHBFX->IOPolledFilePollersOpen)
            {
                IOReturn open_result = callbackHBFX->IOPolledFilePollersOpen(callbackHBFX->file_vars, kIOPolledBeforeSleepState, true);
                if (open_result != KERN_SUCCESS)
                    SYSLOG("HBFX", "IOPolledFilePollersOpen returned error %d", open_result);
                else
                    DBGLOG("HBFX", "IOPolledFilePollersOpen returned success");
            }
            
            OSData *rtc = OSDynamicCast(OSData, IOService::getPMRootDomain()->getProperty(gIOHibernateRTCVariablesKey));
            if (rtc && !callbackHBFX->dtNvram->getProperty(gIOHibernateRTCVariablesKey))
            {
                if (!callbackHBFX->dtNvram->setProperty(gIOHibernateRTCVariablesKey, rtc))
                    SYSLOG("HBFX", "IOHibernateRTCVariablesKey can't be written to NVRAM.");
                else
                {
                    SYSLOG("HBFX", "IOHibernateRTCVariablesKey has been written to NVRAM.");
        
                    // we should remove fakesmc-key-HBKP-ch8* if it exists
                    if (callbackHBFX->dtNvram->getProperty(gFakeSMCHBKP))
                    {
                        callbackHBFX->dtNvram->removeProperty(gFakeSMCHBKP);
                        SYSLOG("HBFX", "fakesmc-key-HBKP-ch8* has been removed from NVRAM.");
                    }
                }
            }
            
            OSData *smc = OSDynamicCast(OSData, IOService::getPMRootDomain()->getProperty(gIOHibernateSMCVariables));
            if (smc && !callbackHBFX->dtNvram->getProperty(gIOHibernateSMCVariables))
            {
                if (!callbackHBFX->dtNvram->setProperty(gIOHibernateSMCVariables, smc))
                    SYSLOG("HBFX", "IOHibernateSMCVariablesKey can't be written to NVRAM.");
            }
            
            if (config.dumpNvram)
            {
                if (OSData *data = OSDynamicCast(OSData, callbackHBFX->dtNvram->getProperty(gIOHibernateBoot0082Key)))
                {
                    if (!callbackHBFX->dtNvram->setProperty(gBoot0082Key, data))
                        SYSLOG("HBFX", "Boot0082 can't be written!");
                }
                else
                    SYSLOG("HBFX", "Variable Boot0082 can't be found!");
                
                if (OSData *data = OSDynamicCast(OSData, callbackHBFX->dtNvram->getProperty(gIOHibernateBootNextKey)))
                {
                    if (!callbackHBFX->dtNvram->setProperty(gBootNextKey, data))
                        SYSLOG("HBFX", "BootNext can't be written!");
                }
                else
                    SYSLOG("HBFX", "Variable BootNext can't be found!");
                
                callbackHBFX->writeNvramToFile();
                
                if (callbackHBFX->sync)
                    callbackHBFX->sync(kernproc, nullptr, nullptr);
                
                callbackHBFX->dtNvram->removeProperty(gBoot0082Key);
                callbackHBFX->dtNvram->removeProperty(gBootNextKey);
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
                    OSData *data = OSData::withBytes(inbuf, part_size);
                    
                    snprintf(key, sizeof(key), "AAPL,PanicInfo%04d", counter++);
                    callbackHBFX->dtNvram->setProperty(OSSymbol::withCString(key), data);
                    pi_size -= part_size;
                    inbuf += part_size;
                }
                
                callbackHBFX->writeNvramToFile();
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

IOReturn HBFX::IOPolledFilePollersSetup(void * vars, uint32_t openState)
{
    IOReturn result = KERN_SUCCESS;
    if (callbackHBFX && callbackHBFX->orgIOPolledFilePollersSetup)
    {
        result = callbackHBFX->orgIOPolledFilePollersSetup(vars, openState);
        
        if (openState == kIOPolledPreflightState)
        {
            callbackHBFX->file_vars_valid = (result == KERN_SUCCESS);
            if (callbackHBFX->file_vars_valid)
            {
                DBGLOG("HBFX", "IOPolledFilePollersSetup called with state kIOPolledPreflightState");
                lilu_os_memcpy(callbackHBFX->file_vars, vars, sizeof(callbackHBFX->file_vars));
                callbackHBFX->file_vars_valid = true;
            }
        }
    }
        
    return result;
}

//==============================================================================

void HBFX::processKernel(KernelPatcher &patcher)
{
    if (IORegistryEntry *options = OSDynamicCast(IORegistryEntry, IORegistryEntry::fromPath("/options", gIODTPlane)))
    {
        if (IODTNVRAM *nvram = OSDynamicCast(IODTNVRAM, options))
        {
            dtNvram = nvram;
            DBGLOG("HBFX", "IODTNVRAM object is aquired");
        }
        else
        {
            SYSLOG("HBFX", "Registry entry /options can't be casted to IONVRAM.");
            return;
        }
    }
    else
    {
        SYSLOG("HBFX", "Registry entry /options is not found.");
        return;
    }

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
    
    method_address = patcher.solveSymbol(KernelPatcher::KernelID, "__Z24IOPolledFilePollersSetupP18IOPolledFileIOVarsj");
    if (method_address) {
        DBGLOG("HBFX", "obtained __Z24IOPolledFilePollersSetupP18IOPolledFileIOVarsj");
        orgIOPolledFilePollersSetup = reinterpret_cast<t_iopolled_file_pollers_setup>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOPolledFilePollersSetup), true));
        if (patcher.getError() == KernelPatcher::Error::NoError) {
            DBGLOG("HBFX", "routed __Z24IOPolledFilePollersSetupP18IOPolledFileIOVarsj");
        } else {
            SYSLOG("HBFX", "failed to route __Z24IOPolledFilePollersSetupP18IOPolledFileIOVarsj");
        }
    } else {
        SYSLOG("HBFX", "failed to resolve __Z24IOPolledFilePollersSetupP18IOPolledFileIOVarsj");
    }
    
    method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_IOPolledFilePollersOpen");
    if (method_address) {
        DBGLOG("HBFX", "obtained _IOPolledFilePollersOpen");
        IOPolledFilePollersOpen = reinterpret_cast<t_iopolled_file_pollers_open>(method_address);
    } else {
        SYSLOG("HBFX", "failed to resolve _IOPolledFilePollersOpen");
    }
    
    
    // Ignore all the errors for other processors
    patcher.clearError();
}

//==============================================================================

void HBFX::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size)
{
    if (progressState != ProcessingState::EverythingDone)
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
    
        if (!(progressState & ProcessingState::KernelRouted))
        {
            processKernel(patcher);
            progressState |= ProcessingState::KernelRouted;
        }
    }
    
    // Ignore all the errors for other processors
    patcher.clearError();
}



//==============================================================================

bool HBFX::writeNvramToFile()
{
    //serialize and write this out
    OSSerialize *s = OSSerialize::withCapacity(80000);
    s->addString(NVRAM_FILE_HEADER);
    dtNvram->serializeProperties(s);
    s->addString(NVRAM_FILE_FOOTER);

    int error = FileIO::writeBufferToFile(FILE_NVRAM_NAME, s->text(), strlen(s->text()));
    
    //now free the dictionaries && iter
    s->release();
    
    return (error == 0);
}
