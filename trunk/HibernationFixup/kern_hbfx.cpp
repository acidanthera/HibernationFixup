//
//  kern_UBFX.cpp
//  UBFX
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#include <Headers/kern_api.hpp>
#include <Headers/kern_file.hpp>
#include <Headers/kern_util.hpp>

#include "kern_config.hpp"
#include "kern_hbfx.hpp"

#include <IOKit/IORegistryEntry.h>
#include <IOKit/IOReportTypes.h>
#include <IOKit/IOService.h>
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


//==============================================================================

bool HBFX::init() {
    LiluAPI::Error error = lilu.onPatcherLoad(
	[](void *user, KernelPatcher &patcher) {
		callbackHBFX = static_cast<HBFX *>(user);
		callbackPatcher = &patcher;
		callbackHBFX->processKernel(patcher);
	}, this);
	
	if (error != LiluAPI::Error::NoError) {
		SYSLOG("HBFX @ failed to register onPatcherLoad method %d", error);
		return false;
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

void HBFX::deinit() {
}

//==============================================================================

IOReturn HBFX::IOHibernateSystemSleep(void) {
    
    IOReturn result = KERN_SUCCESS;
    
    if (callbackHBFX && callbackHBFX->orgIOHibernateSystemSleep && callbackHBFX->dtNvram)
    {
        result = callbackHBFX->orgIOHibernateSystemSleep();
        DBGLOG("HBFX @ IOHibernateSystemSleep is called, result is: %x", result);
        
        uint32_t ioHibernateState = kIOHibernateStateInactive;
        OSData *data = OSDynamicCast(OSData, IOService::getPMRootDomain()->getProperty(kIOHibernateStateKey));
        if (data != 0)
        {
            ioHibernateState = *((uint32_t *)data->getBytesNoCopy());
            DBGLOG("HBFX @ Current hibernate state from IOPMRootDomain is: %d", ioHibernateState);
        }
        
        if (result == KERN_SUCCESS || ioHibernateState == kIOHibernateStateHibernating)
        {
            OSData *rtc = OSDynamicCast(OSData, IOService::getPMRootDomain()->getProperty(gIOHibernateRTCVariablesKey));
            if (rtc && !callbackHBFX->dtNvram->getProperty(gIOHibernateRTCVariablesKey))
            {
                if (!callbackHBFX->dtNvram->setProperty(gIOHibernateRTCVariablesKey, rtc))
                    SYSLOG("HBFX @ IOHibernateRTCVariablesKey can't be written to NVRAM.");
                else
                {
                    SYSLOG("HBFX @ IOHibernateRTCVariablesKey has been written to NVRAM.");
        
                    // we should remove fakesmc-key-HBKP-ch8* if it exists
                    if (callbackHBFX->dtNvram->getProperty(gFakeSMCHBKP))
                    {
                        callbackHBFX->dtNvram->removeProperty(gFakeSMCHBKP);
                        SYSLOG("HBFX @ fakesmc-key-HBKP-ch8* has been removed from NVRAM.");
                    }
                }
            }
            
            OSData *smc = OSDynamicCast(OSData, IOService::getPMRootDomain()->getProperty(gIOHibernateSMCVariables));
            if (smc && !callbackHBFX->dtNvram->getProperty(gIOHibernateSMCVariables))
            {
                if (!callbackHBFX->dtNvram->setProperty(gIOHibernateSMCVariables, smc))
                    SYSLOG("HBFX @ IOHibernateSMCVariablesKey can't be written to NVRAM.");
            }
            
            if (config.dumpNvram)
            {
                if (OSData *data = OSDynamicCast(OSData, callbackHBFX->dtNvram->getProperty(gIOHibernateBoot0082Key)))
                {
                    if (!callbackHBFX->dtNvram->setProperty(gBoot0082Key, data))
                        SYSLOG("HBFX @ Boot0082 can't be written!");
                }
                
                if (OSData *data = OSDynamicCast(OSData, callbackHBFX->dtNvram->getProperty(gIOHibernateBootNextKey)))
                {
                    if (!callbackHBFX->dtNvram->setProperty(gBootNextKey, data))
                        SYSLOG("HBFX @ BootNext can't be written!");
                }
                
                callbackHBFX->writeNvramToFile(callbackHBFX->dtNvram);
                
                if (callbackHBFX->sync)
                    callbackHBFX->sync(kernproc, nullptr, nullptr);
                
                callbackHBFX->dtNvram->removeProperty(gBoot0082Key);
                callbackHBFX->dtNvram->removeProperty(gBootNextKey);
            }
        }
    }
    else {
        SYSLOG("HBFX @ callback arrived at nowhere");
    }

    return result;
}

//==============================================================================

int HBFX::packA(char *inbuf, uint32_t length, uint32_t buflen)
{
    if (callbackHBFX && !callbackHBFX->ml_at_interrupt_context())
    {
        callbackHBFX->ml_set_interrupts_enabled(TRUE);
        if (callbackHBFX->ml_get_interrupts_enabled())
        {
            while (!callbackHBFX->preemption_enabled())
            {
                callbackHBFX->enable_preemption();
                if (!callbackHBFX->ml_get_interrupts_enabled())
                    callbackHBFX->ml_set_interrupts_enabled(TRUE);
            }
            
            FileIO::writeBufferToFile("/panic.info", inbuf, length);
            callbackHBFX->sync(kernproc, nullptr, nullptr);
            
            callbackHBFX->disable_preemption();
            callbackHBFX->ml_set_interrupts_enabled(FALSE);
        }
    }
    
    int bufpos = 0;
    if (callbackHBFX && callbackHBFX->orgPackA)
        bufpos = callbackHBFX->orgPackA(inbuf, length, buflen);
    
    return bufpos;
}

//==============================================================================

void HBFX::unpackA(char *inbuf, uint32_t length)
{
    if (callbackHBFX && !callbackHBFX->ml_at_interrupt_context())
    {
        callbackHBFX->ml_set_interrupts_enabled(TRUE);
        if (callbackHBFX->ml_get_interrupts_enabled())
        {
            while (!callbackHBFX->preemption_enabled())
            {
                callbackHBFX->enable_preemption();
                if (!callbackHBFX->ml_get_interrupts_enabled())
                    callbackHBFX->ml_set_interrupts_enabled(TRUE);
            }
            
            if (callbackHBFX->writeNvramToFile(callbackHBFX->dtNvram))
                callbackHBFX->sync(kernproc, nullptr, nullptr);
            
            callbackHBFX->disable_preemption();
            callbackHBFX->ml_set_interrupts_enabled(FALSE);
        }
    }
    
    if (callbackHBFX && callbackHBFX->orgUnpackA)
        callbackHBFX->orgUnpackA(inbuf, length);
}

//==============================================================================

void HBFX::processKernel(KernelPatcher &patcher)
{
    if (IORegistryEntry *options = OSDynamicCast(IORegistryEntry, IORegistryEntry::fromPath("/options", gIODTPlane)))
    {
        if (IODTNVRAM *nvram = OSDynamicCast(IODTNVRAM, options))
        {
            dtNvram = nvram;
            DBGLOG("HBFX @ IODTNVRAM object is aquired");
        }
        else
            SYSLOG("HBFX @ Registry entry /options can't be casted to IONVRAM.");
    }
    else
        SYSLOG("HBFX @ Registry entry /options is not found.");

    auto sessionCallback = patcher.solveSymbol(KernelPatcher::KernelID, "_IOHibernateSystemSleep");
    if (sessionCallback) {
        DBGLOG("HBFX @ obtained _IOHibernateSystemSleep");
        orgIOHibernateSystemSleep = reinterpret_cast<t_io_hibernate_system_sleep_callback>(patcher.routeFunction(sessionCallback, reinterpret_cast<mach_vm_address_t>(IOHibernateSystemSleep), true));
        if (patcher.getError() == KernelPatcher::Error::NoError) {
            DBGLOG("HBFX @ routed _IOHibernateSystemSleep");
        } else {
            SYSLOG("HBFX @ failed to route _IOHibernateSystemSleep");
        }
    } else {
        SYSLOG("HBFX @ failed to resolve _IOHibernateSystemSleep");
    }
    
    if (config.dumpNvram)
    {
        sessionCallback = patcher.solveSymbol(KernelPatcher::KernelID, "_sync");
        if (sessionCallback) {
            DBGLOG("HBFX @ obtained _sync");
            sync = reinterpret_cast<t_sync>(sessionCallback);
        } else {
            SYSLOG("HBFX @ failed to resolve _sync");
        }
        
        sessionCallback = patcher.solveSymbol(KernelPatcher::KernelID, "_preemption_enabled");
        if (sessionCallback) {
            DBGLOG("HBFX @ obtained _preemption_enabled");
            preemption_enabled = reinterpret_cast<t_preemption_enabled>(sessionCallback);
        } else {
            SYSLOG("HBFX @ failed to resolve _preemption_enabled");
        }
        
        sessionCallback = patcher.solveSymbol(KernelPatcher::KernelID, "__enable_preemption");
        if (sessionCallback) {
            DBGLOG("HBFX @ obtained __enable_preemption");
            enable_preemption = reinterpret_cast<t_enable_preemption>(sessionCallback);
        } else {
            SYSLOG("HBFX @ failed to resolve __enable_preemption");
        }
        
        sessionCallback = patcher.solveSymbol(KernelPatcher::KernelID, "__disable_preemption");
        if (sessionCallback) {
            DBGLOG("HBFX @ obtained __disable_preemption");
            disable_preemption = reinterpret_cast<t_disable_preemption>(sessionCallback);
        } else {
            SYSLOG("HBFX @ failed to resolve __disable_preemption");
        }
        
        sessionCallback = patcher.solveSymbol(KernelPatcher::KernelID, "_ml_at_interrupt_context");
        if (sessionCallback) {
            DBGLOG("HBFX @ obtained _ml_at_interrupt_context");
            ml_at_interrupt_context = reinterpret_cast<t_ml_at_interrupt_context>(sessionCallback);
        } else {
            SYSLOG("HBFX @ failed to resolve _ml_at_interrupt_context");
        }
        
        sessionCallback = patcher.solveSymbol(KernelPatcher::KernelID, "_ml_get_interrupts_enabled");
        if (sessionCallback) {
            DBGLOG("HBFX @ obtained _ml_get_interrupts_enabled");
            ml_get_interrupts_enabled = reinterpret_cast<t_ml_get_interrupts_enabled>(sessionCallback);
        } else {
            SYSLOG("HBFX @ failed to resolve _ml_get_interrupts_enabled");
        }
        
        sessionCallback = patcher.solveSymbol(KernelPatcher::KernelID, "_ml_set_interrupts_enabled");
        if (sessionCallback) {
            DBGLOG("HBFX @ obtained _ml_set_interrupts_enabled");
            ml_set_interrupts_enabled = reinterpret_cast<t_ml_set_interrupts_enabled>(sessionCallback);
        } else {
            SYSLOG("HBFX @ failed to resolve _ml_set_interrupts_enabled");
        }
        
        if (sync && preemption_enabled && enable_preemption && disable_preemption && ml_at_interrupt_context &&
            ml_get_interrupts_enabled && ml_set_interrupts_enabled)
        {
            sessionCallback = patcher.solveSymbol(KernelPatcher::KernelID, "_packA");
            if (sessionCallback) {
                DBGLOG("HBFX @ obtained _packA");
                orgPackA = reinterpret_cast<t_pack_a>(patcher.routeFunction(sessionCallback, reinterpret_cast<mach_vm_address_t>(packA), true));
                if (patcher.getError() == KernelPatcher::Error::NoError) {
                    DBGLOG("HBFX @ routed _packA");
                } else {
                    SYSLOG("HBFX @ failed to route _packA");
                }
            } else {
                SYSLOG("HBFX @ failed to resolve _packA");
            }
            
            sessionCallback = patcher.solveSymbol(KernelPatcher::KernelID, "_unpackA");
            if (sessionCallback) {
                DBGLOG("HBFX @ obtained _unpackA");
                orgUnpackA = reinterpret_cast<t_unpack_a>(patcher.routeFunction(sessionCallback, reinterpret_cast<mach_vm_address_t>(unpackA), true));
                if (patcher.getError() == KernelPatcher::Error::NoError) {
                    DBGLOG("HBFX @ routed _unpackA");
                } else {
                    SYSLOG("HBFX @ failed to route _unpackA");
                }
            } else {
                SYSLOG("HBFX @ failed to resolve _unpackA");
            }
        }
    }
    
    // Ignore all the errors for other processors
    patcher.clearError();
}

//==============================================================================

bool HBFX::writeNvramToFile(IODTNVRAM *nvram)
{
    char filepath[] = {FILE_NVRAM_NAME};
    //DBGLOG("HBFX @ Nvram file path = %s\n", filepath);
    
    //serialize and write this out
    OSSerialize *s = OSSerialize::withCapacity(10000);
    s->addString(NVRAM_FILE_HEADER);
    nvram->serializeProperties(s);
    s->addString(NVRAM_FILE_FOOTER);

    int error = FileIO::writeBufferToFile(filepath, s->text(), strlen(s->text()));
    //if (error)
    //    SYSLOG("HBFX @ Unable to write to %s, errno %d\n", filepath, error);
    
    //now free the dictionaries && iter
    s->release();
    
    return (error == 0);
}
