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
#include <IOKit/pwr_mgt/IOPMPowerSource.h>
#include <IOKit/IOWorkLoop.h>

#include "osx_defines.h"

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
	bool initializeNVStorage();
	
	// read supported options from NVRAM
	void readConfigFromNVRAM();
	
	// return pointer to IOPMPowerSource
	IOPMPowerSource *getPowerSource();
	
	// return true if standby/autopoweroff is enabled
	bool isStandbyEnabled(uint32_t &standby_delay, bool &pmset_default_mode);
	
	IOReturn explicitlyCallSetMaintenanceWakeCalendar();
	
	void checkCapacity();
	
	/**
	 *  Hooked methods / callbacks
	 */
	static IOReturn     IOHibernateSystemSleep(void);
	static IOReturn     IOHibernateSystemWake(void);
	
	static void         IOPMrootDomain_evaluatePolicy(IOPMrootDomain* that, int stimulus, uint32_t arg);
	static void         IOPMrootDomain_requestFullWake(IOPMrootDomain* that, uint32_t reason);
	static IOReturn     IOPMrootDomain_setMaintenanceWakeCalendar(IOPMrootDomain* that, IOPMCalendarStruct * calendar);
	static IOReturn     AppleRTC_setupDateTimeAlarm(void *that, void* rtcDateTime);
	static IOReturn     X86PlatformPlugin_sleepPolicyHandler(void * target, IOPMSystemSleepPolicyVariables * vars, IOPMSystemSleepParameters * params);
	
	static int          packA(char *inbuf, uint32_t length, uint32_t buflen);
	static IOReturn     IOPCIBridge_restoreMachineState(IOService *that, IOOptionBits options, IOService * device);
	static void         IOPCIDevice_extendedConfigWrite16(IOService *that, UInt64 offset, UInt16 data);
	
	
	/**
	 *  Trampolines for original method invocations
	 */
	mach_vm_address_t orgIOHibernateSystemSleep {};
	mach_vm_address_t orgIOHibernateSystemWake {};
	
	mach_vm_address_t orgIOPMrootDomain_evaluatePolicy {};
	mach_vm_address_t orgIOPMrootDomain_requestFullWake {};
	mach_vm_address_t orgIOPMrootDomain_setMaintenanceWakeCalendar {};
	mach_vm_address_t orgX86PlatformPlugin_sleepPolicyHandler {};
	mach_vm_address_t orgAppleRTC_setupDateTimeAlarm {};
	mach_vm_address_t orgPackA {};
	mach_vm_address_t orgIOPCIBridge_restoreMachineState {};
	mach_vm_address_t orgIOPCIDevice_extendedConfigWrite16 {};
	
	/**
	 *  Sync file buffers & interrupts & preemption control
	 */
	using t_sync = int (*) (__unused proc_t p, __unused struct sync_args *uap, __unused int32_t *retval);
	t_sync sync {nullptr};
	
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
	
	using t_convertDateTimeToSeconds = int64_t (*) (void *rtcDateTime);
	t_convertDateTimeToSeconds convertDateTimeToSeconds {nullptr};
	
	using t_convertSecondsToDateTime = int64_t (*) (int64_t seconds, void *rtcDateTime);
	t_convertSecondsToDateTime convertSecondsToDateTime {nullptr};
	
	using t_checkSystemSleepEnabled = bool (*) (IOPMrootDomain* that);
	t_checkSystemSleepEnabled checkSystemSleepEnabled {nullptr};
	
	bool    correct_pci_config_command {false};
	
	uint32_t    latestStandbyDelay {0};
	uint32_t    latestPoweroffDelay {0};
	uint32_t    latestHibernateMode {0};
	uint32_t    sleepPhase {-1U};
	uint64_t    sleepFactors {0};
	uint32_t    sleepReason {0};
	uint32_t    sleepType {0};
	uint32_t    sleepFlags {0};
	bool        sleepServiceWake {false};
	bool        wakeCalendarSet {false};
	
	/**
	 *  Current progress mask
	 */
	struct ProcessingState {
		enum {
			NothingReady = 0,
			IOPCIFamilyRouted = 1,
			KernelRouted = 2,
			AppleRTCRouted = 4,
			X86PluginRouted = 8,
			EverythingDone = IOPCIFamilyRouted | KernelRouted | AppleRTCRouted | X86PluginRouted,
		};
	};
	int progressState {ProcessingState::NothingReady};
	
	NVStorage nvstorage;
	IOWorkLoop *workLoop {};
	IOTimerEventSource *nextSleepTimer {};
	IOTimerEventSource *checkCapacityTimer {};
	bool emulatedNVRAM {false};
#ifdef DEBUG
	int lastStimulus {};
#endif
};

#endif /* kern_hbfx_hpp */
