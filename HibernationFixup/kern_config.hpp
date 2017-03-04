//
//  kern_config_private.hpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
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
    static constexpr const char *bootargHfile {"hfile"};    // hibernate file
		
public:
	/**
	 *  Retrieve boot arguments
	 *
	 *  @return true if allowed to continue
	 */
	void readArguments();
	
	/**
	 *  Disable the extension by default
	 */
    bool dumpNvram {false};
    
    /**
     *  Hibernation file path
     */
    char hfilepath[MAXPATHLEN+1] = {};
	
	
#ifdef DEBUG
	static constexpr const char *fullName {xStringify(PRODUCT_NAME) " Kernel Extension " xStringify(MODULE_VERSION) " DEBUG build"};
#else
	static constexpr const char *fullName {xStringify(PRODUCT_NAME) " Kernel Extension " xStringify(MODULE_VERSION)};
#endif
	
    Configuration() = default;
};

extern Configuration config;

#endif /* kern_config_private_h */
