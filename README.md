HibernationFixup
==================

[![Build Status](https://github.com/acidanthera/HibernationFixup/actions/workflows/main.yml/badge.svg?branch=master)](https://github.com/acidanthera/HibernationFixup/actions) [![Scan Status](https://scan.coverity.com/projects/16402/badge.svg?flat=1)](https://scan.coverity.com/projects/16402)

An open source kernel extension providing a sync between RTC variables and NVRAM.
By design the mach kernel encrypts hibernate sleepimage and writes the encryption key to variable 
"IOHibernateRTCVariables" in the system registry (PMRootDomain).
Somehow this value has to be written into RTC (or SMC) in order the boot.efi could read it.
But in case if you have to limit your RTC memory to 1 bank (128 bytes), it doesn't work: there are no any variables in SMC/NVRAM/RTC (actually FakeSMC). 

Fortunately, boot.efi can read key "IOHibernateRTCVariables" from NVRAM!
This kext detects entering into "hibernate" power state, reads variable 
IOHibernateRTCVariables from the system registry and writes it to NVRAM.

#### Features
- Enables 'native' hibernation on PC's with hardware NVRAM on 10.10.5 and later.
  'Native' means hibernation with encryption (standard hibernate modes 3 & 25)
- Enables dumping NVRAM to file /nvram.plist before hibernation or panic
- Enable patching IOPCIFamily in order to avoid hang & black screen after resume.
  This patch is dynamic, it works only for hibernation and not for regular sleep.
  It is automatically enabled in macOS Sierra 10.12.1 or later and disabled in older macOS versions.

#### Boot-args
- `-hbfxoff` disables kext loading
- `-hbfxdbg` turns on debugging output
- `-hbfxbeta` enables loading on unsupported macOS
- `-hbfx-dump-nvram` saves NVRAM to a file nvram.plist before hibernation and after kernel panic (with panic info)
- `hbfx-patch-pci=XHC,IMEI,IGPU` allows to specify explicit device list (and restoreMachineState won't be called only for these devices). Also supports values `none`, `false`, `off`.
- `-hbfx-disable-patch-pci` disables patching of IOPCIFamily (this patch helps to avoid hang & black screen after resume (restoreMachineState won't be called for all devices))
- `hbfx-ahbm=abhm_value` controls auto-hibernation feature, where abhm_value is an arithmetic sum of respective values below:
	- `EnableAutoHibernation` = 1:
		If this flag is set, system will hibernate instead of regular sleep (flags below can be used to limit this behavior)
	- `WhenLidIsClosed` = 2:
		Auto hibernation can happen when lid is closed (if bit is not set - no matter which status lid has)
	- `WhenExternalPowerIsDisconnected` = 4:
		Auto hibernation can happen when external power is disconnected (if bit is not set - no matter whether it is connected)
	- `WhenBatteryIsNotCharging` = 8:
		Auto hibernation can happen when battery is not charging (if bit is not set - no matter whether it is charging)
	- `WhenBatteryIsAtWarnLevel` = 16:
		Auto hibernation can happen when battery is at warning level (macOS and battery kext are responsible for this level)
	- `WhenBatteryAtCriticalLevel` = 32:
		Auto hibernation can happen when battery is at critical level (macOS and battery kext are responsible for this level)
	- `DoNotOverrideWakeUpTime` = 64:
		Do not alter next wake up time, macOS is fully responsible for sleep maintenance dark wakes
	- `DisableStimulusDarkWakeActivityTickle` = 128:
		Disable power event kStimulusDarkWakeActivityTickle in kernel, so this event cannot trigger a switching from dark wake to full wake

    When when IOPMPowerSource cannot detect warning or critical battery level (it happens on hackintosh), 
	next 4 bits can be used to specify minimal capacity percent remaining value when hibernation will be forced:
	- `RemainCapacityBit1` = 256  (1)
	- `RemainCapacityBit2` = 512  (2)
	- `RemainCapacityBit3` = 1024 (4)
	- `RemainCapacityBit4` = 2048 (8)
	
	Specified minimal capacity will be also used to put macOS into sleep/hibernate state (when the remaining capacity is less than it).
	4 bits can be used to specify the battery levels from 1 to 15. Bits RemainCapacityBit1-RemainCapacityBit4 are 1,2,4,8 in percentage, so for example if you want to have 
	10 percent level to be the point where the laptop goes into sleep/hibernation, you would add Bits RemainCapacityBit4 and RemainCapacityBit2 which would be 2048+512=2560 (8+2=10 percent) in hbfx-ahbm. Bit EnableAutoHibernation defines a final state (sleep or hibernate).

#### NVRAM options
The following options can be stored in NVRAM (GUID = E09B9297-7928-4440-9AAB-D1F8536FBF0A), they can be used instead of respective boot-args
- `hbfx-dump-nvram`  - type Boolean
- `hbfx-disable-patch-pci`  - type Boolean
- `hbfx-patch-pci=XHC,IMEI,IGPU,none,false,off` - type String
- `hbfx-ahbm` - type Number


#### Dependencies
- [Lilu](https://github.com/acidanthera/Lilu)

#### Credits
- [Apple](https://www.apple.com) for macOS  
- [vit9696](https://github.com/vit9696) for [Lilu.kext](https://github.com/vit9696/Lilu) and great help in implementing some features 
- [lvs1974](https://applelife.ru/members/lvs1974.53809/) for writing the software and maintaining it
