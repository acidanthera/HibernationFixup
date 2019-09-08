//
//  kern_config.hpp
//  HibernationFixup
//
//  Copyright © 2016-2017 lvs1974. All rights reserved.
//

#ifndef kern_config_private_h
#define kern_config_private_h

#include <Headers/kern_util.hpp>

class Configuration {
public:
	/**
	 *  Possible boot arguments
	 */
	static const char *bootargOff[];
	static const char *bootargDebug[];
	static const char *bootargBeta[];
	static constexpr const char *bootargDumpNvram         {"-hbfx-dump-nvram"};          // write NVRAM to file
	static constexpr const char *bootargDisablePatchPCI   {"-hbfx-disable-patch-pci"};   // disable patch pci family
	static constexpr const char *bootargPatchPCIWithList  {"hbfx-patch-pci"};            // patch pci family ignored device list
	static constexpr const char *bootargAutoHibernateMode {"hbfx-ahbm"};		    	 // auto hibernate mode

public:
	/**
	 *  Retrieve boot arguments
	 */
	void readArguments();

	/**
	 *  dump nvram to /nvram.plist
	 */
	bool dumpNvram {false};

	/**
	 *  patch PCI Family
	 */
	bool patchPCIFamily {true};

	/**
	 *  device list (can be separated by comma, space or something like that)
	 */
	char ignored_device_list[64] {};
	
	/* Flags used to control automatic hibernation behavior (takes place only when system goes to sleep)
	 */
	enum AutoHibernateModes {
		// If this flag is set, system will hibernate instead of regular sleep (flags below can be used to limit this behavior)
		EnableAutoHibernation           = 1,
		// Auto hibernation can happen when lid is closed (if bit is not set - no matter which status lid has)
		WhenLidIsClosed                 = 2,
		// Auto hibernation can happen when external power is disconnected (if bit is not set - no matter whether it is connected)
		WhenExternalPowerIsDisconnected = 4,
		// Auto hibernation can happen when battery is not charging (if bit is not set - no matter whether it is charging)
		WhenBatteryIsNotCharging        = 8,
		// Auto hibernation can happen when battery is at warning level (osx is responsible for this level)
		WhenBatteryIsAtWarnLevel        = 16,
		// Auto hibernation can happen when battery is at critical level (osx is responsible for this level)
		WhenBatteryAtCriticalLevel      = 32,
		// Reserved
		Reserved1    					= 64,
		// Reserved
		Reserverd2						= 128,
		
		// Next 4 bits are used to specify minimal capacity percent remaining value when hibernation will be forced.
		// Can be used together with WhenBatteryIsAtWarnLevel or WhenBatteryAtCriticalLevel, when IOPMPowerSource cannot detect
		// warning or critical battery level
		RemainCapacityBit1				= 256,
		RemainCapacityBit2				= 512,
		RemainCapacityBit3				= 1024,
		RemainCapacityBit4				= 2048
	};
	
	int autoHibernateMode {0};

	Configuration() = default;
};

extern Configuration ADDPR(hbfx_config);

#endif /* kern_config_private_h */
