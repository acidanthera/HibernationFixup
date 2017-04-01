//
//  kern_hbfx.cpp
//  HBFX
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
static const OSSymbol *gBoot0082Key = nullptr;
static const OSSymbol *gBootNextKey = nullptr;
static const OSSymbol *gFakeSMCHBKP = nullptr;

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
    
    if (callbackHBFX && callbackPatcher && callbackHBFX->orgIOHibernateSystemSleep)
    {
        result = callbackHBFX->orgIOHibernateSystemSleep();
        DBGLOG("HBFX @ IOHibernateSystemSleep is called, result is: %x", result);
        
        uint32_t ioHibernateState = kIOHibernateStateInactive;
        OSData *data = OSDynamicCast(OSData, IOService::getPMRootDomain()->getProperty(kIOHibernateStateKey));
        if (data != 0)
        {
            ioHibernateState = *((uint32_t *)data->getBytesNoCopy());
            DBGLOG("HBFX @ Hibernation detected from IOPMRootDomain: state = %d", ioHibernateState);
        }
        
        if (result == KERN_SUCCESS || ioHibernateState == kIOHibernateStateHibernating)
        {
            if (IORegistryEntry *options = OSDynamicCast(IORegistryEntry, IORegistryEntry::fromPath("/options", gIODTPlane)))
            {
                if (IODTNVRAM *nvram = OSDynamicCast(IODTNVRAM, options))
                {
                    OSData *rtc = OSDynamicCast(OSData, IOService::getPMRootDomain()->getProperty(gIOHibernateRTCVariablesKey));
                    if (rtc && !nvram->getProperty(gIOHibernateRTCVariablesKey))
                    {
                        if (!nvram->setProperty(gIOHibernateRTCVariablesKey, rtc))
                            SYSLOG("HBFX @ IOHibernateRTCVariablesKey can't be written to NVRAM.");
                        else
                        {
                            SYSLOG("HBFX @ IOHibernateRTCVariablesKey has been written to NVRAM.");
                
                            // we should remove fakesmc-key-HBKP-ch8* if it exists
                            if (nvram->getProperty(gFakeSMCHBKP))
                            {
                                nvram->removeProperty(gFakeSMCHBKP);
                                SYSLOG("HBFX @ fakesmc-key-HBKP-ch8* has been removed from NVRAM.");
                            }
                        }
                    }
                    
                    OSData *smc = OSDynamicCast(OSData, IOService::getPMRootDomain()->getProperty(gIOHibernateSMCVariables));
                    if (smc && !nvram->getProperty(gIOHibernateSMCVariables))
                    {
                        if (!nvram->setProperty(gIOHibernateSMCVariables, smc))
                            SYSLOG("HBFX @ IOHibernateSMCVariablesKey can't be written to NVRAM.");
                    }
                    
                    if (config.dumpNvram)
                    {
                        if (OSData *data = OSDynamicCast(OSData, nvram->getProperty(gIOHibernateBoot0082Key)))
                        {
                            if (!nvram->setProperty(gBoot0082Key, data))
                                SYSLOG("HBFX @ Boot0082 can't be written!");
                        }
                        
                        if (OSData *data = OSDynamicCast(OSData, nvram->getProperty(gIOHibernateBootNextKey)))
                        {
                            if (!nvram->setProperty(gBootNextKey, data))
                                SYSLOG("HBFX @ BootNext can't be written!");
                        }
                        
                        callbackHBFX->writeNvramToFile(nvram);
               
                        //if (callbackHBFX->hibernate_setup && callbackHBFX->gIOHibernateCurrentHeader)
                        //    callbackHBFX->hibernate_setup(callbackHBFX->gIOHibernateCurrentHeader, true, 0, 0, 0);
                        
                        if (getKernelVersion() > KernelVersion::MountainLion && callbackHBFX->sync)
                        {
                            int retval;
                            callbackHBFX->sync(kernproc, nullptr, &retval);
                        }
                        
                        nvram->removeProperty(gBoot0082Key);
                        nvram->removeProperty(gBootNextKey);
                    }
                }
                else
                    SYSLOG("HBFX @ Registry entry /options can't be casted to IONVRAM.");
                
                OSSafeReleaseNULL(options);
            }
            else
                SYSLOG("HBFX @ Registry entry /options is not found.");
        }
    }
    else {
        SYSLOG("HBFX @ callback arrived at nowhere");
    }

    return result;
}

//==============================================================================

void HBFX::processKernel(KernelPatcher &patcher)
{
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
    
//    sessionCallback = patcher.solveSymbol(KernelPatcher::KernelID, "_hibernate_setup");
//    if (sessionCallback) {
//        DBGLOG("HBFX @ obtained _hibernate_setup");
//        hibernate_setup = reinterpret_cast<t_hibernate_setup>(sessionCallback);
//    } else {
//        SYSLOG("HBFX @ failed to resolve _hibernate_setup");
//    }
//    
//    gIOHibernateCurrentHeader = reinterpret_cast<void *>(patcher.solveSymbol(KernelPatcher::KernelID, "_gIOHibernateCurrentHeader"));
//    if (gIOHibernateCurrentHeader) {
//        DBGLOG("HBFX @ obtained _gIOHibernateCurrentHeader");
//    } else {
//        SYSLOG("HBFX @ failed to resolve _gIOHibernateCurrentHeader");
//    }
    
    sessionCallback = patcher.solveSymbol(KernelPatcher::KernelID, "_sync");
    if (sessionCallback) {
        DBGLOG("HBFX @ obtained _sync");
        sync = reinterpret_cast<t_sync>(sessionCallback);
    } else {
        SYSLOG("HBFX @ failed to resolve _sync");
    }
    
    // Ignore all the errors for other processors
    patcher.clearError();
}

//==============================================================================

void HBFX::writeNvramToFile(IODTNVRAM *nvram)
{
    char filepath[] = {FILE_NVRAM_NAME};
    DBGLOG("HBFX @ Nvram file path = %s\n", filepath);
    
    //serialize and write this out
    OSSerialize *s = OSSerialize::withCapacity(10000);
    s->addString(NVRAM_FILE_HEADER);
    nvram->serializeProperties(s);
    s->addString(NVRAM_FILE_FOOTER);

    int error = FileIO::writeBufferToFile(filepath, s->text(), strlen(s->text()));
    if (error)
        SYSLOG("HBFX @ Unable to write to %s, errno %d\n", filepath, error);
    
    //now free the dictionaries && iter
    s->release();
}
