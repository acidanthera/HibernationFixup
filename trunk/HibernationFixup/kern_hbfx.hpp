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
#define kFakeSMCHBKB                "fakesmc-key-HBKP-ch8*"

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
	 *  PAVP session callback type
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
     *  external global variables
     */
    uint8_t *gIOFBVerboseBootPtr {nullptr};
};

struct AppleRTCHibernateVars
{
    uint8_t     signature[4];
    uint32_t    revision;
    uint8_t	    booterSignature[20];
    uint8_t	    wiredCryptKey[16];
};

#endif /* kern_hbfx_hpp */
