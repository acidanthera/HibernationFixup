//
//  kern_start.cpp
//  IntelGraphicsFixup
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


Configuration config;


void Configuration::readArguments() {
    char tmp[20];
    if (PE_parse_boot_argn(bootargDumpNvram, tmp, sizeof(tmp)))
        dumpNvram = true;
    
    if (PE_parse_boot_argn(bootargPatchPCI, tmp, sizeof(tmp)))
        patchPCIFamily = true;
}



PluginConfiguration ADDPR(config) {
	xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
	config.bootargOff,
	sizeof(config.bootargOff)/sizeof(config.bootargOff[0]),
	config.bootargDebug,
	sizeof(config.bootargDebug)/sizeof(config.bootargDebug[0]),
	config.bootargBeta,
	sizeof(config.bootargBeta)/sizeof(config.bootargBeta[0]),
	KernelVersion::MountainLion,
	KernelVersion::Sierra,
	[]() {
        config.readArguments();
		hbfx.init();
	}
};





