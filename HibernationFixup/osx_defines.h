//
//  osx_defines.h
//  HibernationFixup
//
//  Copyright © 2019 lvs1974. All rights reserved.
//

#ifndef osx_defines_h
#define osx_defines_h

#define kIOHibernateStateKey            "IOHibernateState"
#define kIOHibernateRTCVariablesKey     "IOHibernateRTCVariables"
#define kIOHibernateSMCVariablesKey     "IOHibernateSMCVariables"
#define kIOHibernateFileKey             "Hibernate File"
#define kBoot0082Key                    "Boot0082"
#define kBootNextKey                    "BootNext"
#define kGlobalBoot0082Key              NVRAM_PREFIX(NVRAM_GLOBAL_GUID, kBoot0082Key)
#define kGlobalBootNextKey              NVRAM_PREFIX(NVRAM_GLOBAL_GUID, kBootNextKey)

#define kAppleClamshellStateKey         "AppleClamshellState"
#define kAppleClamshellCausesSleepKey   "AppleClamshellCausesSleep"
#define kAppleSleepDisabled				"SleepDisabled"


struct IOPMSystemSleepPolicyVariables
{
	uint32_t    signature;                  // kIOPMSystemSleepPolicySignature
	uint32_t    version;                    // kIOPMSystemSleepPolicyVersion
	
	uint64_t    currentCapability;          // current system capability bits
	uint64_t    highestCapability;          // highest system capability bits
	
	uint64_t    sleepFactors;               // sleep factor bits
	uint32_t    sleepReason;                // kIOPMSleepReason*
	uint32_t    sleepPhase;                 // identify the sleep phase
	uint32_t    hibernateMode;              // current hibernate mode
	
	uint32_t    standbyDelay;               // standby delay in seconds
	uint32_t    standbyTimer;               // standby timer in seconds
	uint32_t    poweroffDelay;              // auto-poweroff delay in seconds
	uint32_t    scheduledAlarms;            // bitmask of scheduled alarm types
	uint32_t    poweroffTimer;              // auto-poweroff timer in seconds
	
	uint32_t    reserved[49];               // pad sizeof 256 bytes
};

struct IOPMSystemSleepParameters
{
	uint16_t    version;
	uint16_t    reserved1;
	uint32_t    sleepType;
	uint32_t    sleepFlags;
	uint32_t    ecWakeEvents;
	uint32_t    ecWakeTimer;
	uint32_t    ecPoweroffTimer;
	uint32_t    reserved2[10];
} __attribute__((packed));


#define kIOPMSystemSleepPolicySignature     0x54504c53
#define kIOPMSystemSleepPolicyVersion       2

/*! @constant kIOPMCalendarWakeTypes
 *
 * These are valid values for IOPM.h:IOPMCalendarStruct->selector
 */
enum {
	kPMCalendarTypeMaintenance = 1,
	kPMCalendarTypeSleepService = 2
};


// gIOHibernateState, kIOHibernateStateKey
enum
{
	kIOHibernateStateInactive            = 0,
	kIOHibernateStateHibernating         = 1,	/* writing image */
	kIOHibernateStateWakingFromHibernate = 2	/* booted and restored image */
};

enum
{
	kMachineRestoreBridges      = 0x00000001,
	kMachineRestoreEarlyDevices = 0x00000002,
	kMachineRestoreDehibernate  = 0x00000004,
	kMachineRestoreTunnels      = 0x00000008,
};

/* Command register definitions */
enum {
	kIOPCICommandIOSpace                = 0x0001,
	kIOPCICommandMemorySpace            = 0x0002,
	kIOPCICommandBusMaster              = 0x0004,
	kIOPCICommandSpecialCycles          = 0x0008,
	kIOPCICommandMemWrInvalidate        = 0x0010,
	kIOPCICommandPaletteSnoop           = 0x0020,
	kIOPCICommandParityError            = 0x0040,
	kIOPCICommandAddressStepping        = 0x0080,
	kIOPCICommandSERR                   = 0x0100,
	kIOPCICommandFastBack2Back          = 0x0200,
	kIOPCICommandInterruptDisable       = 0x0400
};

// System Sleep Types
enum {
	kIOPMSleepTypeInvalid                   = 0,
	kIOPMSleepTypeAbortedSleep              = 1,
	kIOPMSleepTypeNormalSleep               = 2,
	kIOPMSleepTypeSafeSleep                 = 3,
	kIOPMSleepTypeHibernate                 = 4,
	kIOPMSleepTypeStandby                   = 5,
	kIOPMSleepTypePowerOff                  = 6,
	kIOPMSleepTypeDeepIdle                  = 7,
	kIOPMSleepTypeLast                      = 8
};

enum {
	kIOPMSleepReasonClamshell                   = 101,
	kIOPMSleepReasonPowerButton                 = 102,
	kIOPMSleepReasonSoftware                    = 103,
	kIOPMSleepReasonOSSwitchHibernate           = 104,
	kIOPMSleepReasonIdle                        = 105,
	kIOPMSleepReasonLowPower                    = 106,
	kIOPMSleepReasonThermalEmergency            = 107,
	kIOPMSleepReasonMaintenance                 = 108,
	kIOPMSleepReasonSleepServiceExit            = 109,
	kIOPMSleepReasonDarkWakeThermalEmergency    = 110
};

// Sleep Factor Mask / Bits
enum {
	kIOPMSleepFactorSleepTimerWake          = 0x00000001ULL,
	kIOPMSleepFactorLidOpen                 = 0x00000002ULL,
	kIOPMSleepFactorACPower                 = 0x00000004ULL,
	kIOPMSleepFactorBatteryLow              = 0x00000008ULL,
	kIOPMSleepFactorStandbyNoDelay          = 0x00000010ULL,
	kIOPMSleepFactorStandbyForced           = 0x00000020ULL,
	kIOPMSleepFactorStandbyDisabled         = 0x00000040ULL,
	kIOPMSleepFactorUSBExternalDevice       = 0x00000080ULL,
	kIOPMSleepFactorBluetoothHIDDevice      = 0x00000100ULL,
	kIOPMSleepFactorExternalMediaMounted    = 0x00000200ULL,
	kIOPMSleepFactorThunderboltDevice       = 0x00000400ULL,
	kIOPMSleepFactorRTCAlarmScheduled       = 0x00000800ULL,
	kIOPMSleepFactorMagicPacketWakeEnabled  = 0x00001000ULL,
	kIOPMSleepFactorHibernateForced         = 0x00010000ULL,
	kIOPMSleepFactorAutoPowerOffDisabled    = 0x00020000ULL,
	kIOPMSleepFactorAutoPowerOffForced      = 0x00040000ULL,
	kIOPMSleepFactorExternalDisplay         = 0x00080000ULL,
	kIOPMSleepFactorNetworkKeepAliveActive  = 0x00100000ULL,
	kIOPMSleepFactorLocalUserActivity       = 0x00200000ULL,
	kIOPMSleepFactorHibernateFailed         = 0x00400000ULL,
	kIOPMSleepFactorThermalWarning          = 0x00800000ULL,
	kIOPMSleepFactorDisplayCaptured         = 0x01000000ULL
};

// Sleep flags
enum {
	kIOPMSleepFlagHibernate         = 0x00000001,
	kIOPMSleepFlagSleepTimerEnable  = 0x00000002
};


#endif /* osx_defines_h */
