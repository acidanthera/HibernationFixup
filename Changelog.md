HibernationFixup Changelog
============================
#### v1.0.0
- Initial release

#### v1.1.0
- Write NVRAM to file

#### v1.1.1
- Call file sync method (to be sure that nvram.plist will be written)


#### v1.1.2
- Works with EmuVariable

#### v1.1.3
- Panic handling and writing to nvram.plist

#### v1.1.4
- Fix system freeze and black screen when resume after hibernation (Sierra only)

#### v1.1.5
- Added OSBundleCompatibleVersion

#### v1.1.6
- Requires Lilu 1.1.6
- Compatibility with High Sierra
- PCI Family patch was improved
- New boot-arg hbfx-patch-pci=[comma-separated list of ignored devices] supported
