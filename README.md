HibernationFixup
==================

An open source kernel extension providing a sync between RTC variables and NVRAM.
By design the mach kernel encrypts hibernate sleepimage and writes the encryption key to variable 
"IOHibernateRTCVariables" in the system registry (PMRootDomain).
Somehow this value has to be written into RTC (or SMC) in order the boot.efi could read it.
But in my case it doesn't work: there are no any variables in SMC (actually FakeSMC). 
I use FakeSMC with possibility to save SMC keys to NVRAM
and Clover with possibility to restore these keys from NVRAM and provide SMC Protocol (SMCHelper.efi) for boot.efi.

Fortunately, boot.efi can read key "IOHibernateRTCVariables" from NVRAM!
This kext detects entering into "hibernate" power state, reads variable 
IOHibernateRTCVariables from the system registry and writes it to NVRAM.

#### Features
- Enables 'native' hibernation on PC's with hardware NVRAM on 10.10.5 and later.
  'Native' means hibernation with encryption (standard hibernate modes 3 & 25)
- Enables dumping NVRAM to file /nvram.plist before hibernation or panic


#### Credits
- [Apple](https://www.apple.com) for macOS  
- [vit9696](https://github.com/vit9696) for [Lilu.kext](https://github.com/vit9696/Lilu) and great help in implementing some features 
- [lvs1974](https://applelife.ru/members/lvs1974.53809/) for writing the software and maintaining it
