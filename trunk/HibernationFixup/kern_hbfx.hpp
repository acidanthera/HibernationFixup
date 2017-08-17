//
//  kern_hbfx.hpp
//  HBFX
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#ifndef kern_hbfx_hpp
#define kern_hbfx_hpp

#include <Headers/kern_patcher.hpp>
#include <IOKit/IOService.h>

#define kIOHibernateStateKey            "IOHibernateState"
#define kIOHibernateRTCVariablesKey     "IOHibernateRTCVariables"
#define kIOHibernateSMCVariablesKey     "IOHibernateSMCVariables"
#define kFakeSMCHBKB                    "fakesmc-key-HBKP-ch8*"
#define kIOHibernateFileKey             "Hibernate File"

#define FILE_NVRAM_NAME                 "/nvram.plist"

#define NVRAM_FILE_HEADER               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" \
                                        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"\
                                        "<plist version=\"1.0\">\n"
#define NVRAM_FILE_FOOTER               "\n</plist>\n"


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
     *  Patch kext if needed and prepare other patches
     *
     *  @param patcher KernelPatcher instance
     *  @param index   kinfo handle
     *  @param address kinfo load address
     *  @param size    kinfo memory size
     */
    void processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
    
	/**
	 *  IOHibernateSystemSleep callback type
	 */
	using t_io_hibernate_system_sleep_callback = IOReturn (*)(void);
    
    /**
     *  packA callback type
     */
    using t_pack_a = UInt32 (*) (char *inbuf, uint32_t length, uint32_t buflen);
    
    /**
     *  unpackA callback type
     */
    using t_unpack_a = UInt32 (*) (char *inbuf, uint32_t length);
 
    /**
     *  restoreMachineState callback type
     */
    using t_restore_machine_state = IOReturn (*) (IOService *that, IOOptionBits options, IOService * device);
    
    /**
     *  extendedConfigWrite16 callback type
     */
    using t_extended_config_write16 = void (*) (IOService *that, UInt64 offset, UInt16 data);
    
    
	/**
	 *  Hooked methods / callbacks
	 */
    static IOReturn     IOHibernateSystemSleep(void);
    static uint32_t     hibernate_write_image(void);
    static int          packA(char *inbuf, uint32_t length, uint32_t buflen);
    static IOReturn     restoreMachineState(IOService *that, IOOptionBits options, IOService * device);
    static void         extendedConfigWrite16(IOService *that, UInt64 offset, UInt16 data);
    
	/**
	 *  Trampolines for original method invocations
	 */
    t_io_hibernate_system_sleep_callback    orgIOHibernateSystemSleep {nullptr};
    t_pack_a                                orgPackA {nullptr};
    t_restore_machine_state                 orgRestoreMachineState {nullptr};
    t_extended_config_write16               orgExtendedConfigWrite16 {nullptr};
    
    /**
     *  Write NVRAM to file
     */
    bool writeNvramToFile();

    
    /**
     *  Sync file buffers
     */
    using t_sync = int (*) (__unused proc_t p, __unused struct sync_args *uap, __unused int32_t *retval);
    t_sync sync;
    
    using t_preemption_enabled = boolean_t (*) (void);
    t_preemption_enabled preemption_enabled {nullptr};

    using t_enable_preemption = void (*) (void);
    t_enable_preemption enable_preemption {nullptr};
    
    using t_disable_preemption = void (*) (void);
    t_disable_preemption disable_preemption {nullptr};
    
    using t_ml_at_interrupt_context = boolean_t (*) (void);
    t_ml_at_interrupt_context ml_at_interrupt_context {nullptr};
    
    using t_ml_get_interrupts_enabled = boolean_t (*) (void);
    t_ml_get_interrupts_enabled ml_get_interrupts_enabled {nullptr};
    
    using t_ml_set_interrupts_enabled = boolean_t (*) (boolean_t enable);
    t_ml_set_interrupts_enabled ml_set_interrupts_enabled {nullptr};
    
    IODTNVRAM *dtNvram {nullptr};
    
    uint8_t *extended_config_write16 {nullptr};
    uint8_t original_code[128];
    size_t  instruction_size {0};
    bool    disable_pci_config_command {false};

    
    /**
     *  Current progress mask
     */
    struct ProcessingState {
        enum {
            NothingReady = 0,
            IOPCIFamilyRouted = 2,
            KernelRouted = 4,
            EverythingDone = IOPCIFamilyRouted | KernelRouted,
        };
    };
    int progressState {ProcessingState::NothingReady};
};

#endif /* kern_hbfx_hpp */
