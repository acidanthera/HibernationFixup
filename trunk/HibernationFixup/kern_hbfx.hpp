//
//  kern_hbfx.hpp
//  HBFX
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#ifndef kern_hbfx_hpp
#define kern_hbfx_hpp

#include <Headers/kern_patcher.hpp>

#define kIOHibernateRTCVariablesKey	"IOHibernateRTCVariables"
#define kIOHibernateSMCVariablesKey	"IOHibernateSMCVariables"
#define kFakeSMCHBKB                "fakesmc-key-HBKP-ch8*"

#define kIOHibernateFileKey		"Hibernate File"
#define FILE_NVRAM_NAME			"nvram.plist"
#define NVRAM_FILE_HEADER		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" \
                                "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"\
                                "<plist version=\"1.0\">\n"
#define NVRAM_FILE_FOOTER		"\n</plist>\n"

class IODTNVRAM;

class HBFX {
public:
	bool init();
	void deinit();
	
private:
	/**
	 *  Patch kernel
	 *
	 *  @param patcher KernelPatcher instance
	 */
    void processKernel(KernelPatcher &patcher);
	
	/**
	 *  IOHibernateSystemSleep callback type
	 */
	using t_io_hibernate_system_sleep_callback = IOReturn (*)(void);
    
	/**
	 *  Hooked methods / callbacks
	 */
    static IOReturn IOHibernateSystemSleep(void);

	/**
	 *  Trampolines for original method invocations
	 */
    t_io_hibernate_system_sleep_callback orgIOHibernateSystemSleep {nullptr};
    
    /**
     *  Write NVRAM to file
     */

    void writeNvramToFile(IODTNVRAM *nvram);
};

#endif /* kern_hbfx_hpp */
