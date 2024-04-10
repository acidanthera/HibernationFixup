HibernationFixup Changelog
============================

#### v1.5.0
- Auto hibernation: make sure auto-hibernate feature only happens when hibernatemode is set to 3. (Never with mode 0 or 25).

#### v1.4.9
- Add macOS 14 (Sonoma) constants

#### v1.4.8
- Take into account a full wake event only if the local user provoked this event (stabilize switching to hibernate mode)

#### v1.4.7
- Respect parameters `standbydelaylow`, `standbydelayhigh`, `highstandbythreshold` set via pmset utility
- Introduce a new bit in hbfx-ahbm boot-arg: `DoNotOverrideWakeUpTime` = 64, to let macOS decide when wake for standby sleep maintenance

#### v1.4.6
- Added constants for macOS 13 support
- Decouple WhenBatteryIsNotCharging/WhenBatteryIsAtWarnLevel and minimal capacity in parameter `hbfx-ahbm`

#### v1.4.5
- When battery level is critical, try to put macOS into sleep/hibernate mode only once per minute.

#### v1.4.4
- Automatically puts macOS into sleep/hibernate mode when WhenBatteryIsAtWarnLevel or WhenBatteryAtCriticalLevel bit is set in hbfx-ahbm. If battery kext does not provide these levels, additional bits for remaining capacity can be specified (RemainCapacityBit1, RemainCapacityBit2, RemainCapacityBit3, RemainCapacityBit4).

#### v1.4.3
- Use method routeMultipleLong instead of routeMultiple in order to avoid conflict with DebugEnhancer

#### v1.4.2
- Use method routeMultipleLong instead of routeMultiple in order to avoid conflict with future versions of CpuTscSync

#### v1.4.1
- Added constants for macOS 12 support.

#### v1.4.0
- Auto hibernation: added possibility to disable power event kStimulusDarkWakeActivityTickle in kernel, so this event cannot be a trigger for switching from dark wake to full wake.
Can be turned on via bit `DisableStimulusDarkWakeActivityTickle=128` in boot-arg `hbfx-ahbm`.
- Support options in NVRAM (GUID = E09B9297-7928-4440-9AAB-D1F8536FBF0A or LiluReadOnlyGuid)

#### v1.3.9
- Auto hibernation: properly handle transition from dark wake to full wake
- Extend method emuVariableIsDetected in order to use EfiRuntimeServices if nvram cannot be accessed in standard way

#### v1.3.8
- Improve auto hibernation: immediate hibernate if standbydelaylow / autopoweroffdelay is 0, fast resume after hibernate, code cleanup

#### v1.3.7
- Refactoring, setup next RTC wake manually if IOPMrootDomain::setMaintenanceWakeCalendar was not called before sleep
- Force next sleep if maintanence wake type is detected (RTC/SleepService/Maintenance)

#### v1.3.6
- Added MacKernelSDK with Xcode 12 compatibility

#### v1.3.5
- Postpone RTC wake in AppleRTC::setupDateTimeAlarm according to current standby/autopoweroff delay (if auto-hibernate feature and standby/autopoweroff is on)
in order to avoid earlier wake.

#### v1.3.4
- Improve auto-hibernate feature: correct next wake time disregarding the current sleep phase.
- Added constants for 11.0 support.

#### v1.3.3
- Improve auto-hibernate feature: support standby and autopoweroff separately (with respective delay). Immediate hibernate is possible with zero delay.

#### v1.3.2
-  Fix nvram.plist saving in Catalina, new path is used when the root folder is not writable: /System/Volumes/Data/nvram.plist

#### v1.3.1
- Code refactoring, fix duplicates in log, auto-hibernate can work without power source

#### v1.3.0
- Do not use recursive iterator to detect EmuVariableUefiPresent  (based on panic report analysis)

#### v1.2.9
- Improve auto-hibernate feature: if power nap is enabled, hibernation will start after next Maintenance/SleepService wake (standbyDelay value is respected)

#### v1.2.8
- Fixed memory leaks
- Fixed EmuVariableUefiPresent detection (nvram.plist will be properly saved if EmuVariableUefiPresent == Yes in ioreg)
- Improve auto-hibernate feature: modify next wake time only before regular sleep

#### v1.2.7
- Unified release archive names

#### v1.2.6
- Allow loading on 10.15 without `-lilubetaall`
- Fix minor typos in code and comments (credits to PMheart)

#### v1.2.5
- Improve auto-hibernate feature: modify next wake time to currentTime + standbyDelay

#### v1.2.4
- New feature:  forces hibernate mode depending on specified factors (auto hibernate modes)

#### v1.2.3
- Basic 10.14 support

#### v1.2.2
- Fix a name conflict for config variable
- Improve pci patch (allow to write to PCI config command register, but bit `memory space` must be always set )

#### v1.2.1
- Save hibernation keys in NVRAM only if boot-arg `-hbfx-dump-nvram` is specified or if the second bank of RTC memory (next block of 128 bytes) is not available
- PCI Family patch is always enabled, boot-arg `-hbfx-patch-pci` is obsolete. A new boot arg `-hbfx-disable-patch-pci` is introduced to disable any patching

#### v1.1.7
- Fixes for 1.1.6b (Release was non-working)
- Use pollers to provoke writing of SMC-keys earlier

#### v1.1.6
- Requires Lilu 1.1.6
- Compatibility with High Sierra
- PCI Family patch was improved
- New boot-arg hbfx-patch-pci=[comma-separated list of ignored devices] supported

#### v1.1.5
- Added OSBundleCompatibleVersion

#### v1.1.4
- Fix system freeze and black screen when resume after hibernation (Sierra only)

#### v1.1.3
- Panic handling and writing to nvram.plist

#### v1.1.2
- Works with EmuVariable

#### v1.1.1
- Call file sync method (to be sure that nvram.plist will be written)

#### v1.1.0
- Write NVRAM to file

#### v1.0.0
- Initial release
