//
//  kern_hbfx.cpp
//  HBFX
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#include <Headers/kern_api.hpp>
#include <Headers/kern_file.hpp>

#include "kern_config.hpp"
#include "kern_hbfx.hpp"

#include <IOKit/IORegistryEntry.h>
#include <IOKit/IOReportTypes.h>
#include <IOKit/IOService.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IONVRAM.h>



// Only used in apple-driven callbacks
static HBFX *callbackHBFX = nullptr;
static KernelPatcher *callbackPatcher = nullptr;
static const OSSymbol *gIOHibernateRTCVariablesKey = nullptr;
static const OSSymbol *gIOHibernateSMCVariables = nullptr;
static const OSSymbol *gIOHibernateBoot0082Key = nullptr;
static const OSSymbol *gIOHibernateBootNextKey = nullptr;
static const OSSymbol *gBoot0082Key  = nullptr;
static const OSSymbol *gBootNextKey  = nullptr;
static const OSSymbol *gFakeSMCHBKP  = nullptr;

extern proc_t kernproc;


// gIOHibernateState, kIOHibernateStateKey
enum
{
    kIOHibernateStateInactive            = 0,
    kIOHibernateStateHibernating         = 1,	/* writing image */
    kIOHibernateStateWakingFromHibernate = 2	/* booted and restored image */
};

static const char *kextIOPCIFamilyPath[] { "/System/Library/Extensions/IOPCIFamily.kext/IOPCIFamily" };

static KernelPatcher::KextInfo kextList[] {
    { "com.apple.iokit.IOPCIFamily", kextIOPCIFamilyPath, 1, true, {}, KernelPatcher::KextInfo::Unloaded },
};

static size_t kextListSize {1};



//==============================================================================

bool HBFX::init()
{
    if (config.patchPCIFamily)
    {
        if (!disasm.init()) {
            SYSLOG("HBFX @ failed to use disasm");
            return false;
        }
        
        LiluAPI::Error error = lilu.onKextLoad(kextList, kextListSize,
                                               [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
                                                   callbackHBFX = static_cast<HBFX *>(user);
                                                   callbackPatcher = &patcher;
                                                   callbackHBFX->processKext(patcher, index, address, size);
                                               }, this);
        
        if (error != LiluAPI::Error::NoError) {
            SYSLOG("HBFX @ failed to register onKextLoad method %d", error);
            return false;
        }
    }
    else
    {
        LiluAPI::Error error = lilu.onPatcherLoad(
                                                  [](void *user, KernelPatcher &patcher) {
                                                      callbackHBFX = static_cast<HBFX *>(user);
                                                      callbackPatcher = &patcher;
                                                      callbackHBFX->processKernel(patcher);
                                                  }, this);
        
        if (error != LiluAPI::Error::NoError) {
            SYSLOG("HBFX @ failed to register onPatcherLoad method %d", error);
            return false;
        }
    }
    
    gIOHibernateRTCVariablesKey = OSSymbol::withCStringNoCopy(kIOHibernateRTCVariablesKey);
    gIOHibernateSMCVariables    = OSSymbol::withCStringNoCopy(kIOHibernateSMCVariablesKey);
    gIOHibernateBoot0082Key     = OSSymbol::withCString("8BE4DF61-93CA-11D2-AA0D-00E098032B8C:Boot0082");
    gIOHibernateBootNextKey     = OSSymbol::withCString("8BE4DF61-93CA-11D2-AA0D-00E098032B8C:BootNext");
    gBoot0082Key                = OSSymbol::withCString("Boot0082");
    gBootNextKey                = OSSymbol::withCString("BootNext");
    gFakeSMCHBKP                = OSSymbol::withCStringNoCopy(kFakeSMCHBKB);
    
	return true;
}

//==============================================================================

void HBFX::deinit()
{
    if (config.patchPCIFamily)
    {
        // Deinitialise disassembler
        disasm.deinit();
    }
}

//==============================================================================

bool HBFX::patchIOPCIFamily()
{
    bool result = false;
    if (extended_config_write16)
    {
        bool interrupts_enabled = ml_get_interrupts_enabled();
        if (MachInfo::setKernelWriting(true) == KERN_SUCCESS)
        {
            memset(extended_config_write16, 0x90, instruction_size);
            DBGLOG("HBFX::restoreMachineState is patched");
            MachInfo::setKernelWriting(false);
            result = true;
        }
        else
            SYSLOG("MachInfo::setKernelWriting failed");
        ml_set_interrupts_enabled(interrupts_enabled);
    }
    
    return result;
}

//==============================================================================

bool HBFX::restoreIOPCIFamily()
{
    bool result = false;
    if (extended_config_write16)
    {
        bool interrupts_enabled = ml_get_interrupts_enabled();
        if (MachInfo::setKernelWriting(true) == KERN_SUCCESS)
        {
            memcpy(extended_config_write16, original_code, instruction_size);
            DBGLOG("HBFX::restoreMachineState is restored");
            MachInfo::setKernelWriting(false);
        }
        else
            SYSLOG("MachInfo::setKernelWriting failed");
        ml_set_interrupts_enabled(interrupts_enabled);
    }
    
    return result;
}

//==============================================================================

bool HBFX::isIOPCIFamilyPatched()
{
    return (extended_config_write16 && memcmp(extended_config_write16, original_code, instruction_size));
}

//==============================================================================

IOReturn HBFX::IOHibernateSystemSleep(void) {
    
    IOReturn result = KERN_SUCCESS;
    
    if (callbackHBFX && callbackHBFX->orgIOHibernateSystemSleep && callbackHBFX->dtNvram)
    {
        result = callbackHBFX->orgIOHibernateSystemSleep();
        DBGLOG("HBFX @ IOHibernateSystemSleep is called, result is: %x", result);

        uint32_t ioHibernateState = kIOHibernateStateInactive;
        OSData *data = OSDynamicCast(OSData, IOService::getPMRootDomain()->getProperty(kIOHibernateStateKey));
        if (data != nullptr)
        {
            ioHibernateState = *((uint32_t *)data->getBytesNoCopy());
            DBGLOG("HBFX @ Current hibernate state from IOPMRootDomain is: %d", ioHibernateState);
        }
        
        if (result == KERN_SUCCESS || ioHibernateState == kIOHibernateStateHibernating)
        {
            OSData *rtc = OSDynamicCast(OSData, IOService::getPMRootDomain()->getProperty(gIOHibernateRTCVariablesKey));
            if (rtc && !callbackHBFX->dtNvram->getProperty(gIOHibernateRTCVariablesKey))
            {
                if (!callbackHBFX->dtNvram->setProperty(gIOHibernateRTCVariablesKey, rtc))
                    SYSLOG("HBFX @ IOHibernateRTCVariablesKey can't be written to NVRAM.");
                else
                {
                    SYSLOG("HBFX @ IOHibernateRTCVariablesKey has been written to NVRAM.");
        
                    // we should remove fakesmc-key-HBKP-ch8* if it exists
                    if (callbackHBFX->dtNvram->getProperty(gFakeSMCHBKP))
                    {
                        callbackHBFX->dtNvram->removeProperty(gFakeSMCHBKP);
                        SYSLOG("HBFX @ fakesmc-key-HBKP-ch8* has been removed from NVRAM.");
                    }
                }
            }
            
            OSData *smc = OSDynamicCast(OSData, IOService::getPMRootDomain()->getProperty(gIOHibernateSMCVariables));
            if (smc && !callbackHBFX->dtNvram->getProperty(gIOHibernateSMCVariables))
            {
                if (!callbackHBFX->dtNvram->setProperty(gIOHibernateSMCVariables, smc))
                    SYSLOG("HBFX @ IOHibernateSMCVariablesKey can't be written to NVRAM.");
            }
            
            if (config.dumpNvram)
            {
                if (OSData *data = OSDynamicCast(OSData, callbackHBFX->dtNvram->getProperty(gIOHibernateBoot0082Key)))
                {
                    if (!callbackHBFX->dtNvram->setProperty(gBoot0082Key, data))
                        SYSLOG("HBFX @ Boot0082 can't be written!");
                }
                
                if (OSData *data = OSDynamicCast(OSData, callbackHBFX->dtNvram->getProperty(gIOHibernateBootNextKey)))
                {
                    if (!callbackHBFX->dtNvram->setProperty(gBootNextKey, data))
                        SYSLOG("HBFX @ BootNext can't be written!");
                }
                
                callbackHBFX->writeNvramToFile();
                
                if (callbackHBFX->sync)
                    callbackHBFX->sync(kernproc, nullptr, nullptr);
                
                callbackHBFX->dtNvram->removeProperty(gBoot0082Key);
                callbackHBFX->dtNvram->removeProperty(gBootNextKey);
            }
            
            if (config.patchPCIFamily)
                callbackHBFX->patchIOPCIFamily();
        }
        else if (config.patchPCIFamily && callbackHBFX->isIOPCIFamilyPatched())
        {
            callbackHBFX->restoreIOPCIFamily();
        }
    }
    else {
        SYSLOG("HBFX @ callback arrived at nowhere");
    }

    return result;
}

//==============================================================================

//uint32_t HBFX::hibernate_write_image(void)
//{
//    uint32_t result = 0;
//    
//    if (callbackHBFX && callbackHBFX->orgHibernateWriteImage && callbackHBFX->dtNvram)
//    {
//        result = callbackHBFX->orgHibernateWriteImage();
//        
//        OSData *rtc = OSDynamicCast(OSData, IOService::getPMRootDomain()->getProperty(gIOHibernateRTCVariablesKey));
//        if (rtc)
//        {
//            if (!callbackHBFX->dtNvram->setProperty(gIOHibernateRTCVariablesKey, rtc))
//                SYSLOG("HBFX @ IOHibernateRTCVariablesKey can't be written to NVRAM.");
//            callbackHBFX->dtNvram->sync();
//            callbackHBFX->dtNvram->syncOFVariables();
//        }
//        
//        // attempt to save a file in hibernate_write_image casues hangs
//        if (config.dumpNvram && !callbackHBFX->ml_at_interrupt_context())
//        {
//            bool interrupts_enabled = callbackHBFX->ml_get_interrupts_enabled();
//            
//            if (OSData *data = OSDynamicCast(OSData, callbackHBFX->dtNvram->getProperty(gIOHibernateBoot0082Key)))
//            {
//                if (!callbackHBFX->dtNvram->setProperty(gBoot0082Key, data))
//                    SYSLOG("HBFX @ Boot0082 can't be written!");
//            }
//            
//            if (OSData *data = OSDynamicCast(OSData, callbackHBFX->dtNvram->getProperty(gIOHibernateBootNextKey)))
//            {
//                if (!callbackHBFX->dtNvram->setProperty(gBootNextKey, data))
//                    SYSLOG("HBFX @ BootNext can't be written!");
//            }
//            
//            callbackHBFX->ml_set_interrupts_enabled(TRUE);
//            while (!callbackHBFX->preemption_enabled())
//            {
//                callbackHBFX->enable_preemption();
//                if (!callbackHBFX->ml_get_interrupts_enabled())
//                    callbackHBFX->ml_set_interrupts_enabled(TRUE);
//            }
//
//            callbackHBFX->writeNvramToFile();
//            
//            if (callbackHBFX->sync)
//                callbackHBFX->sync(kernproc, nullptr, nullptr);
//        
//            callbackHBFX->dtNvram->removeProperty(gBoot0082Key);
//            callbackHBFX->dtNvram->removeProperty(gBootNextKey);
//            
//            callbackHBFX->dtNvram->sync();
//            callbackHBFX->dtNvram->syncOFVariables();
//            
//            callbackHBFX->disable_preemption();
//            callbackHBFX->ml_set_interrupts_enabled(interrupts_enabled);
//        }
//    }
//    
//    return result;
//}


//==============================================================================

int HBFX::packA(char *inbuf, uint32_t length, uint32_t buflen)
{
    char key[128];
    unsigned int bufpos = 0;
    if (callbackHBFX && callbackHBFX->orgPackA)
        bufpos = callbackHBFX->orgPackA(inbuf, length, buflen);
    
    if (callbackHBFX && !callbackHBFX->ml_at_interrupt_context())
    {
        const bool interrupts_enabled = callbackHBFX->ml_get_interrupts_enabled();
        if (!interrupts_enabled)
            callbackHBFX->ml_set_interrupts_enabled(TRUE);
        if (callbackHBFX->ml_get_interrupts_enabled())
        {
            int counter = 10;
            const bool preemption_enabled = callbackHBFX->preemption_enabled();
            while (!callbackHBFX->preemption_enabled() && --counter >= 0)
            {
                callbackHBFX->enable_preemption();
                IOSleep(1);
            }
            
            if (callbackHBFX->preemption_enabled())
            {
                unsigned int pi_size = bufpos ? bufpos : length;
                const unsigned int max_size = 768;
                counter = 0;
                while (pi_size > 0)
                {
                    unsigned int part_size = (pi_size > max_size) ? max_size : pi_size;
                    OSData *data = OSData::withBytes(inbuf, part_size);
                    
                    snprintf(key, sizeof(key), "AAPL,PanicInfo%04d", counter++);
                    callbackHBFX->dtNvram->setProperty(OSSymbol::withCString(key), data);
                    pi_size -= part_size;
                    inbuf += part_size;
                }
                
                callbackHBFX->writeNvramToFile();
                callbackHBFX->sync(kernproc, nullptr, nullptr);
            }
            
            if (!preemption_enabled)
                callbackHBFX->disable_preemption();
            if (!interrupts_enabled)
                callbackHBFX->ml_set_interrupts_enabled(FALSE);
        }
    }
    
    return bufpos;
}

//==============================================================================

void HBFX::processKernel(KernelPatcher &patcher)
{
    if (IORegistryEntry *options = OSDynamicCast(IORegistryEntry, IORegistryEntry::fromPath("/options", gIODTPlane)))
    {
        if (IODTNVRAM *nvram = OSDynamicCast(IODTNVRAM, options))
        {
            dtNvram = nvram;
            DBGLOG("HBFX @ IODTNVRAM object is aquired");
        }
        else
            SYSLOG("HBFX @ Registry entry /options can't be casted to IONVRAM.");
    }
    else
        SYSLOG("HBFX @ Registry entry /options is not found.");

    auto method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_IOHibernateSystemSleep");
    if (method_address) {
        DBGLOG("HBFX @ obtained _IOHibernateSystemSleep");
        orgIOHibernateSystemSleep = reinterpret_cast<t_io_hibernate_system_sleep_callback>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOHibernateSystemSleep), true));
        if (patcher.getError() == KernelPatcher::Error::NoError) {
            DBGLOG("HBFX @ routed _IOHibernateSystemSleep");
        } else {
            SYSLOG("HBFX @ failed to route _IOHibernateSystemSleep");
        }
    } else {
        SYSLOG("HBFX @ failed to resolve _IOHibernateSystemSleep");
    }

//    method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_hibernate_write_image");
//    if (method_address) {
//        DBGLOG("HBFX @ obtained _hibernate_write_image");
//        orgHibernateWriteImage = reinterpret_cast<t_hibernate_write_image>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(hibernate_write_image), true));
//        if (patcher.getError() == KernelPatcher::Error::NoError) {
//            DBGLOG("HBFX @ routed _hibernate_write_image");
//        } else {
//            SYSLOG("HBFX @ failed to route _hibernate_write_image");
//        }
//    } else {
//        SYSLOG("HBFX @ failed to resolve _hibernate_write_image");
//    }
    
    method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_ml_at_interrupt_context");
    if (method_address) {
        DBGLOG("HBFX @ obtained _ml_at_interrupt_context");
        ml_at_interrupt_context = reinterpret_cast<t_ml_at_interrupt_context>(method_address);
    } else {
        SYSLOG("HBFX @ failed to resolve _ml_at_interrupt_context");
    }
    
    method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_ml_get_interrupts_enabled");
    if (method_address) {
        DBGLOG("HBFX @ obtained _ml_get_interrupts_enabled");
        ml_get_interrupts_enabled = reinterpret_cast<t_ml_get_interrupts_enabled>(method_address);
    } else {
        SYSLOG("HBFX @ failed to resolve _ml_get_interrupts_enabled");
    }
    
    method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_ml_set_interrupts_enabled");
    if (method_address) {
        DBGLOG("HBFX @ obtained _ml_set_interrupts_enabled");
        ml_set_interrupts_enabled = reinterpret_cast<t_ml_set_interrupts_enabled>(method_address);
    } else {
        SYSLOG("HBFX @ failed to resolve _ml_set_interrupts_enabled");
    }
    
    if (config.dumpNvram)
    {
        method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_sync");
        if (method_address) {
            DBGLOG("HBFX @ obtained _sync");
            sync = reinterpret_cast<t_sync>(method_address);
        } else {
            SYSLOG("HBFX @ failed to resolve _sync");
        }
        
        method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_preemption_enabled");
        if (method_address) {
            DBGLOG("HBFX @ obtained _preemption_enabled");
            preemption_enabled = reinterpret_cast<t_preemption_enabled>(method_address);
        } else {
            SYSLOG("HBFX @ failed to resolve _preemption_enabled");
        }
        
        method_address = patcher.solveSymbol(KernelPatcher::KernelID, "__enable_preemption");
        if (method_address) {
            DBGLOG("HBFX @ obtained __enable_preemption");
            enable_preemption = reinterpret_cast<t_enable_preemption>(method_address);
        } else {
            SYSLOG("HBFX @ failed to resolve __enable_preemption");
        }
        
        method_address = patcher.solveSymbol(KernelPatcher::KernelID, "__disable_preemption");
        if (method_address) {
            DBGLOG("HBFX @ obtained __disable_preemption");
            disable_preemption = reinterpret_cast<t_disable_preemption>(method_address);
        } else {
            SYSLOG("HBFX @ failed to resolve __disable_preemption");
        }
        
        if (sync && preemption_enabled && enable_preemption && disable_preemption && ml_at_interrupt_context &&
            ml_get_interrupts_enabled && ml_set_interrupts_enabled)
        {
            method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_packA");
            if (method_address) {
                DBGLOG("HBFX @ obtained _packA");
                orgPackA = reinterpret_cast<t_pack_a>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(packA), true));
                if (patcher.getError() == KernelPatcher::Error::NoError) {
                    DBGLOG("HBFX @ routed _packA");
                } else {
                    SYSLOG("HBFX @ failed to route _packA");
                }
            } else {
                SYSLOG("HBFX @ failed to resolve _packA");
            }
        }
    }
    
    // Ignore all the errors for other processors
    patcher.clearError();

}

//==============================================================================

uint8_t *mem_uint8(const void *bigptr, uint8_t ch, size_t length)
{
    const uint8_t *big = (const uint8_t *)bigptr;
    size_t n;
    for (n = 0; n < length; n++)
        if (big[n] == ch)
            return const_cast<uint8_t*>(&big[n]);
    return nullptr;
}

//==============================================================================

uint8_t *HBFX::findCallInstructionInMemory(mach_vm_address_t memory, size_t mem_size, mach_vm_address_t called_method)
{
    uint8_t *curr = reinterpret_cast<uint8_t *>(memory);
    uint8_t *off  = curr + mem_size;
    
    while (curr < off)
    {
        curr = mem_uint8(curr, 0xE8, off - curr);
        if (!curr)
        {
            DBGLOG("HBFX @ findCallInstructionInMemory found no calls");
            break;
        }
        
        size_t isize = disasm.instructionSize(reinterpret_cast<mach_vm_address_t>(curr), 2);
        if (isize == 0)
        {
            DBGLOG("HBFX @ disasm returned zero size insruction");
            return nullptr;
        }
        
        mach_vm_address_t diff = (called_method - reinterpret_cast<mach_vm_address_t>(curr + isize));
        if (!memcmp(curr+1, &diff, isize-1))
            return curr;
        
        curr += isize;
    }
    
    return nullptr;
}

//==============================================================================

void HBFX::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size)
{
    if (progressState != ProcessingState::EverythingDone) {
        if (!(progressState & ProcessingState::IOPCIFamilyTouted) && getKernelVersion() >= KernelVersion::Sierra) {
            for (size_t i = 0; i < kextListSize; i++) {
                if (kextList[i].loadIndex == index && !strcmp(kextList[i].id, "com.apple.iokit.IOPCIFamily")) {
                    auto restoreMachine = patcher.solveSymbol(index, "__ZN11IOPCIBridge19restoreMachineStateEjP11IOPCIDevice");
                    auto extendedConfigWrite = patcher.solveSymbol(index, "__ZN11IOPCIDevice21extendedConfigWrite16Eyt");
                    if (restoreMachine && extendedConfigWrite) {
                        extended_config_write16 = findCallInstructionInMemory(restoreMachine, 1024, extendedConfigWrite);
                        if (extended_config_write16) {
                            DBGLOG("HBFX @ findCallInstructionInMemory found call of __ZN11IOPCIDevice21extendedConfigWrite16Eyt");
                            instruction_size = disasm.instructionSize(reinterpret_cast<mach_vm_address_t>(extended_config_write16), 2);
                            memcpy(original_code, extended_config_write16, instruction_size);
                            progressState |= ProcessingState::IOPCIFamilyTouted;
                        }
                        else {
                            SYSLOG("HBFX @ failed to find call of __ZN11IOPCIDevice21extendedConfigWrite16Eyt");
                        }
                    } else {
                        SYSLOG("HBFX @ failed to resolve __ZN11IOPCIBridge19restoreMachineStateEjP11IOPCIDevice");
                    }
                }
            }
        }
    
        if (!(progressState & ProcessingState::KernelRouted))
        {
            processKernel(patcher);
            progressState |= ProcessingState::KernelRouted;
        }
    }
}



//==============================================================================

bool HBFX::writeNvramToFile()
{
    //serialize and write this out
    OSSerialize *s = OSSerialize::withCapacity(80000);
    s->addString(NVRAM_FILE_HEADER);
    dtNvram->serializeProperties(s);
    s->addString(NVRAM_FILE_FOOTER);

    int error = FileIO::writeBufferToFile(FILE_NVRAM_NAME, s->text(), strlen(s->text()));
    
    //now free the dictionaries && iter
    s->release();
    
    return (error == 0);
}
