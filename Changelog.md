HibernationFixup Changelog
============================
#### v1.2.6
- Allow loading on 10.15 without `-lilubetaall`

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
