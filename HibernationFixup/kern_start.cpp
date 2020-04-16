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
	dumpNvram = checkKernelArgument(bootargDumpNvram);

	if (getKernelVersion() >= KernelVersion::Sierra)
	{
		if (PE_parse_boot_argn(bootargPatchPCIWithList, ignored_device_list, sizeof(ignored_device_list)))
		{
			DBGLOG("HBFX", "ignored device list=%s", ignored_device_list);
			if (strstr(ignored_device_list, "none") != nullptr ||
				strstr(ignored_device_list, "false") != nullptr ||
				strstr(ignored_device_list, "off") != nullptr)
			{
				patchPCIFamily = false;
				DBGLOG("HBFX", "Turn off PCIFamily patching since %s contains none, false or off", bootargPatchPCIWithList);
			}
			else
				DBGLOG("HBFX", "boot-arg %s specified, turn on PCIFamily patching", bootargPatchPCIWithList);
		}
		
		if (checkKernelArgument(bootargDisablePatchPCI))
		{
			patchPCIFamily = false;
			DBGLOG("HBFX", "boot-arg %s specified, turn off PCIFamily patching", bootargDisablePatchPCI);
		}
	}
	
	if (PE_parse_boot_argn(bootargAutoHibernateMode, &autoHibernateMode, sizeof(autoHibernateMode)))
	{
		DBGLOG("HBFX", "boot-arg %s specified, value is 0x%02x", bootargAutoHibernateMode, autoHibernateMode);
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
	KernelVersion::Catalina,
	[]() {
		ADDPR(hbfx_config).readArguments();
		hbfx.init();
	}
};
