//
//  kern_start.cpp
//  HibernationFixup
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>

#include "kern_config.hpp"
#include "kern_hbfx.hpp"

static HBFX hbfx;

const char *Configuration::bootargOff[] {
	"-hbfxoff"
};

const char *Configuration::bootargDebug[] {
	"-hbfxdbg"
};

const char *Configuration::bootargBeta[] {
	"-hbfxbeta"
};


Configuration ADDPR(hbfx_config);


void Configuration::readArguments() {
    char tmp[20];
    if (PE_parse_boot_argn(bootargDumpNvram, tmp, sizeof(tmp)))
        dumpNvram = true;
    
    if (getKernelVersion() >= KernelVersion::Sierra)
    {
        if (PE_parse_boot_argn(bootargPatchPCIWithList, ignored_device_list, sizeof(ignored_device_list)))
        {
            DBGLOG("HBFX", "boot-arg %s specified, turn on PCIFamily patching", bootargPatchPCIWithList);
            DBGLOG("HBFX", "ignored device list=%s", ignored_device_list);
        }
        
        if (PE_parse_boot_argn(bootargDisablePatchPCI, tmp, sizeof(tmp)))
        {
            patchPCIFamily = false;
            DBGLOG("HBFX", "boot-arg %s specified, turn off PCIFamily patching", bootargDisablePatchPCI);
        }
    }
}



PluginConfiguration ADDPR(config) {
	xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal,
	ADDPR(hbfx_config).bootargOff,
	arrsize(ADDPR(hbfx_config).bootargOff),
	ADDPR(hbfx_config).bootargDebug,
	arrsize(ADDPR(hbfx_config).bootargDebug),
	ADDPR(hbfx_config).bootargBeta,
	arrsize(ADDPR(hbfx_config).bootargBeta),
	KernelVersion::MountainLion,
	KernelVersion::HighSierra,
	[]() {
        ADDPR(hbfx_config).readArguments();
		hbfx.init();
	}
};





