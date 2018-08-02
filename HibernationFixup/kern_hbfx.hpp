//
//  kern_hbfx.hpp
//  HibernationFixup
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#ifndef kern_hbfx_hpp
#define kern_hbfx_hpp

#include <Headers/kern_patcher.hpp>
#include <Headers/kern_nvram.hpp>

#define kIOHibernateStateKey            "IOHibernateState"
#define kIOHibernateRTCVariablesKey     "IOHibernateRTCVariables"
#define kIOHibernateSMCVariablesKey     "IOHibernateSMCVariables"
#define kFakeSMCHBKB                    "fakesmc-key-HBKP-ch8*"
#define kIOHibernateFileKey             "Hibernate File"
#define kBoot0082Key                    "Boot0082"
#define kBootNextKey                    "BootNext"
#define kGlobalBoot0082Key              NVRAM_PREFIX(NVRAM_GLOBAL_GUID, kBoot0082Key)
#define kGlobalBootNextKey              NVRAM_PREFIX(NVRAM_GLOBAL_GUID, kBootNextKey)

#define FILE_NVRAM_NAME                 "/nvram.plist"

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
	 *  Check the second RTC memory bank availability
	 */
	bool checkRTCExtendedMemory();
	
	/**
	 *  Initialize NVStorage
	 *
	 */
	bool initialize_nvstorage();
	
	/**
	 *  Hooked methods / callbacks
	 */
	static IOReturn     IOHibernateSystemSleep(void);
	static int          packA(char *inbuf, uint32_t length, uint32_t buflen);
	static IOReturn     restoreMachineState(IOService *that, IOOptionBits options, IOService * device);
	static void         extendedConfigWrite16(IOService *that, UInt64 offset, UInt16 data);
	
	/**
	 *  Trampolines for original method invocations
	 */
	mach_vm_address_t orgIOHibernateSystemSleep {};
	mach_vm_address_t orgPackA {};
	mach_vm_address_t orgRestoreMachineState {};
	mach_vm_address_t orgExtendedConfigWrite16 {};

	/**
	 *  Sync file buffers & interrupts & preemption control
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
	
	using t_iopolled_file_pollers_open = IOReturn (*) (void * vars, uint32_t state, bool abortable);
	t_iopolled_file_pollers_open IOPolledFilePollersOpen {nullptr};
	
	bool    correct_pci_config_command {false};
	
	/**
	 *  Current progress mask
	 */
	struct ProcessingState {
		enum {
			NothingReady = 0,
			IOPCIFamilyRouted = 1,
			KernelRouted = 2,
			EverythingDone = IOPCIFamilyRouted | KernelRouted,
		};
	};
	int progressState {ProcessingState::NothingReady};
	
	NVStorage nvstorage;
};

#endif /* kern_hbfx_hpp */
