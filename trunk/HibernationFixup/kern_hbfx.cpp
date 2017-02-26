//
//  kern_hbfx.cpp
//  HBFX
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#include <Headers/kern_api.hpp>

#include <mach/vm_map.h>

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
static const OSSymbol *gFakeSMCHBKP = nullptr;

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
    gFakeSMCHBKP = OSSymbol::withCStringNoCopy(kFakeSMCHBKB);
    
	return true;
}

void HBFX::deinit() {
}

IOReturn HBFX::IOHibernateSystemSleep(void) {
    
    IOReturn result = KERN_SUCCESS;
    
    if (callbackHBFX && callbackPatcher && callbackHBFX->orgIOHibernateSystemSleep)
    {
        result = callbackHBFX->orgIOHibernateSystemSleep();
        if (result == KERN_SUCCESS)
        {
            DBGLOG("HBFX @ IOHibernateSystemSleep is called, result is: %x", result);
            OSData *data = OSDynamicCast(OSData, IOService::getPMRootDomain()->getProperty(gIOHibernateRTCVariablesKey));
            if (data)
            {
                if (IORegistryEntry *options = OSDynamicCast(IORegistryEntry, IORegistryEntry::fromPath("/options", gIODTPlane)))
                {
                    if (IODTNVRAM *nvram = OSDynamicCast(IODTNVRAM, options))
                    {
                        if (!nvram->getProperty(gIOHibernateRTCVariablesKey))
                        {
                            if (!nvram->setProperty(gIOHibernateRTCVariablesKey, data))
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
                        
                        nvram->sync();
                    }
                    else
                        SYSLOG("HBFX @ Registry entry /options can't be casted to IONVRAM.");
                    
                    OSSafeReleaseNULL(options);
                }
                else
                    SYSLOG("HBFX @ Registry entry /options is not found.");
            }
            else {
                SYSLOG("HBFX @ key '%s' is not found", kIOHibernateRTCVariablesKey);
            }
        }
    }
    else {
        DBGLOG("HBFX @ callback arrived at nowhere");
    }

    return result;
}

void HBFX::processKernel(KernelPatcher &patcher)
{
    auto sessionCallback = patcher.solveSymbol(0, "_IOHibernateSystemSleep");
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
    
    // Ignore all the errors for other processors
    patcher.clearError();
}


// This code can be used to store a real key to fakesmc-key-HBKP-ch8
//    OSData *rtc = OSData::withData(data);
//    if (sizeof(AppleRTCHibernateVars) != rtc->getLength())
//    {
//        DBGLOG("HBFX @ data has wrong size: %u, must be %lu", rtc->getLength(), sizeof(AppleRTCHibernateVars));
//    }
//    else
//    {
//        AppleRTCHibernateVars* rtcVars = (AppleRTCHibernateVars*)rtc->getBytesNoCopy();
//        if (rtcVars->signature[0] != 'A'|| rtcVars->signature[1] != 'A'|| rtcVars->signature[2] != 'P'|| rtcVars->signature[3] != 'L')
//        {
//            DBGLOG("HBFX @ AppleRTCHibernateVars has a wrong signatire");
//        }
//        else
//        {
//            DBGLOG("HBFX @ withBytes -> create new OSData");
//            OSData *data = OSData::withBytes(rtcVars->wiredCryptKey, sizeof(rtcVars->wiredCryptKey));
//            if (data)
//            {
//                memset(rtcVars->wiredCryptKey, 0, sizeof(rtcVars->wiredCryptKey));
//                data->appendBytes(rtcVars->wiredCryptKey, sizeof(rtcVars->wiredCryptKey));
//                if (!nvram->setProperty(gFakeSMCHBKP, data))
//                    SYSLOG("HBFX @ fakesmc-key-HBKP-ch8* can't be written to NVRAM.");
//                    data->release();
//                    }
//            else
//                SYSLOG("HBFX @ OSData::withBytes failed to create OSData");
//                }
//    }
//
//    rtc->release();
