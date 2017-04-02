//
//  kern_hbfx.hpp
//  HBFX
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#ifndef kern_hbfx_hpp
#define kern_hbfx_hpp

#include <Headers/kern_patcher.hpp>

#define kIOHibernateStateKey		"IOHibernateState"
#define kIOHibernateRTCVariablesKey	"IOHibernateRTCVariables"
#define kIOHibernateSMCVariablesKey	"IOHibernateSMCVariables"
#define kFakeSMCHBKB                "fakesmc-key-HBKP-ch8*"
#define kIOHibernateFileKey         "Hibernate File"

#define FILE_NVRAM_NAME             "/nvram.plist"

#define NVRAM_FILE_HEADER           "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" \
                                    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"\
                                    "<plist version=\"1.0\">\n"
#define NVRAM_FILE_FOOTER           "\n</plist>\n"


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
     *  PESavePanicInfo callback type
     */
    using t_save_panic_info = UInt32 (*) (UInt8 *buffer, UInt32 length);
    
	/**
	 *  Hooked methods / callbacks
	 */
    static IOReturn     IOHibernateSystemSleep(void);
    static UInt32       savePanicInfo(UInt8 *buffer, UInt32 length);

	/**
	 *  Trampolines for original method invocations
	 */
    t_io_hibernate_system_sleep_callback    orgIOHibernateSystemSleep {nullptr};
    t_save_panic_info                       orgPESavePanicInfo {nullptr};
    
    /**
     *  Write NVRAM to file
     */
    void writeNvramToFile(IODTNVRAM *nvram);
    
    /**
     *  Sync file buffers
     */
    using t_sync = int (*) (__unused proc_t p, __unused struct sync_args *uap, __unused int32_t *retval);
    t_sync sync;
};

#endif /* kern_hbfx_hpp */
