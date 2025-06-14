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
	if (checkKernelArgument(bootargDumpNvram)) {
		dumpNvram = true;
		DBGLOG("HBFX", "boot-arg %s specified, turn on writing NVRAM to file", bootargDumpNvram);
	}

	if ((getKernelVersion() == KernelVersion::Sierra && getKernelMinorVersion() >= 1) ||
		getKernelVersion() >= KernelVersion::HighSierra)
	{
		if (PE_parse_boot_argn(bootargPatchPCIWithList, ignored_device_list, sizeof(ignored_device_list)))
		{
			DBGLOG("HBFX", "boot-arg %s specified, ignored device list: %s", bootargPatchPCIWithList, ignored_device_list);
			if (strstr(ignored_device_list, "none") != nullptr ||
				strstr(ignored_device_list, "false") != nullptr ||
				strstr(ignored_device_list, "off") != nullptr)
			{
				patchPCIFamily = false;
				DBGLOG("HBFX", "Turn off PCIFamily patching since %s contains none, false or off", bootargPatchPCIWithList);
			}
		}
		
		if (checkKernelArgument(bootargDisablePatchPCI))
		{
			patchPCIFamily = false;
			DBGLOG("HBFX", "boot-arg %s specified, turn off PCIFamily patching", bootargDisablePatchPCI);
		}
	}
	else
	{
		patchPCIFamily = false;
		DBGLOG("HBFX", "Running on Darwin %d.%d. Turn off PCIFamily patching since it is not required in this macOS version", getKernelVersion(), getKernelMinorVersion());
	}

	if (PE_parse_boot_argn(bootargAutoHibernateMode, &autoHibernateMode, sizeof(autoHibernateMode)))
	{
		DBGLOG("HBFX", "boot-arg %s specified, value: %d", bootargAutoHibernateMode, autoHibernateMode);
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
	KernelVersion::Tahoe,
	[]() {
		ADDPR(hbfx_config).readArguments();
		hbfx.init();
	}
};
