//
//  kern_config.hpp
//  HibernationFixup
//
//  Copyright © 2016-2017 lvs1974. All rights reserved.
//

#ifndef kern_config_private_h
#define kern_config_private_h


class Configuration {
public:
	/**
	 *  Possible boot arguments
	 */
    static const char *bootargOff[];
    static const char *bootargDebug[];
    static const char *bootargBeta[];
    static constexpr const char *bootargDumpNvram {"-hbfx-dump-nvram"}; // write NVRAM to file
    static constexpr const char *bootargPatchPCI {"-hbfx-patch-pci"};   // patch pci family
    static constexpr const char *bootargPatchPCIWithList {"hbfx-patch-pci"};   // patch pci family with list
    
public:
	/**
	 *  Retrieve boot arguments
	 *
	 *  @return true if allowed to continue
	 */
	void readArguments();
	
	/**
	 *  dump nvram to /nvram.plist
	 */
    bool dumpNvram {false};
    
    
    /**
     *  patch PCI Family
     */
    bool patchPCIFamily {false};
    
    /**
     *  device list (can be separated by comma, space or something like that)
     */
    char ignored_device_list[64] = {};
	
	
#ifdef DEBUG
	static constexpr const char *fullName {xStringify(PRODUCT_NAME) " Kernel Extension " xStringify(MODULE_VERSION) " DEBUG build"};
#else
	static constexpr const char *fullName {xStringify(PRODUCT_NAME) " Kernel Extension " xStringify(MODULE_VERSION)};
#endif
	
    Configuration() = default;
};

extern Configuration config;

#endif /* kern_config_private_h */
