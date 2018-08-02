HibernationFixup
==================

[![Build Status](https://travis-ci.org/acidanthera/HibernationFixup.svg?branch=master)](https://travis-ci.org/acidanthera/HibernationFixup) [![Scan Status](https://scan.coverity.com/projects/16402/badge.svg?flat=1)](https://scan.coverity.com/projects/16402)

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

#### Boot-args
- `-hbfx-dump-nvram` saves NVRAM to a file nvram.plist before hibernation and after kernel panic (with panic info)
- `-hbfx-disable-patch-pci` disables patching of IOPCIFamily (this patch helps to avoid hang & black screen after resume (restoreMachineState won't be called for all devices)
- `hbfx-patch-pci=XHC,IMEI,IGPU` allows to specify explicit device list and restoreMachineState won't  be called only for these devices)
- `-hbfxdbg` turns on debugging output
- `-hbfxbeta` enables loading on unsupported osx
- `-hbfxoff` disables kext loading

#### Dependencies
- Lilu v1.2.5

#### Credits
- [Apple](https://www.apple.com) for macOS  
- [vit9696](https://github.com/vit9696) for [Lilu.kext](https://github.com/vit9696/Lilu) and great help in implementing some features 
- [lvs1974](https://applelife.ru/members/lvs1974.53809/) for writing the software and maintaining it
