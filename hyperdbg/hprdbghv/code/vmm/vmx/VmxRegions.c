/**
 * @file VmxRegions.c
 * @author Sina Karvandi (sina@hyperdbg.org)
 * @brief Implement allocations for VMX Regions (VMXON Region, VMCS, MSR Bitmap and etc.)
 * @details
 * @version 0.1
 * @date 2020-04-11
 *
 * @copyright This project is released under the GNU Public License v3.
 *
 */
#include "pch.h"

/**
 * @brief Allocates Vmxon region and set the Revision ID based on IA32_VMX_BASIC_MSR
 *
 * @param CurrentGuestState
 * @return BOOLEAN Returns true if allocation was successful and vmxon executed without error
 * otherwise returns false
 */
_Use_decl_annotations_
BOOLEAN
VmxAllocateVmxonRegion(VIRTUAL_MACHINE_STATE * VCpu)
{
    IA32_VMX_BASIC_REGISTER VmxBasicMsr = {0};
    SIZE_T                  VmxonSize;
    UINT8                   VmxonStatus;
    UINT8 *                 VmxonRegion;
    UINT64                  VmxonRegionPhysicalAddr;
    UINT64                  AlignedVmxonRegion;
    UINT64                  AlignedVmxonRegionPhysicalAddr;

#ifdef ENV_WINDOWS
    //
    // at IRQL > DISPATCH_LEVEL memory allocation routines don't work
    //
    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
        KeRaiseIrqlToDpcLevel();
#endif // ENV_WINDOWS

    //
    // Allocating a 4-KByte Contigous Memory region
    //
    VmxonSize   = 2 * VMXON_SIZE;
    VmxonRegion = CrsAllocateContiguousZeroedMemory(VmxonSize + ALIGNMENT_PAGE_SIZE);
    if (VmxonRegion == NULL)
    {
        LogError("Err, couldn't allocate buffer for VMXON region");
        return FALSE;
    }

    VmxonRegionPhysicalAddr = VirtualAddressToPhysicalAddress(VmxonRegion);

    AlignedVmxonRegion = (BYTE *)((ULONG_PTR)(VmxonRegion + ALIGNMENT_PAGE_SIZE - 1) & ~(ALIGNMENT_PAGE_SIZE - 1));
    LogDebugInfo("VMXON Region Address : %llx", AlignedVmxonRegion);

    //
    // 4 kb >= buffers are aligned, just a double check to ensure if it's aligned
    //
    AlignedVmxonRegionPhysicalAddr = (BYTE *)((ULONG_PTR)(VmxonRegionPhysicalAddr + ALIGNMENT_PAGE_SIZE - 1) & ~(ALIGNMENT_PAGE_SIZE - 1));
    LogDebugInfo("VMXON Region Physical Address : %llx", AlignedVmxonRegionPhysicalAddr);

    //
    // get IA32_VMX_BASIC_MSR RevisionId
    //
    VmxBasicMsr.AsUInt = __readmsr(IA32_VMX_BASIC);
    LogDebugInfo("Revision Identifier (IA32_VMX_BASIC - MSR 0x480) : 0x%x", VmxBasicMsr.VmcsRevisionId);

    //
    // Changing Revision Identifier
    //
    *(UINT64 *)AlignedVmxonRegion = VmxBasicMsr.VmcsRevisionId;

    //
    // Execute Vmxon instruction
    //
    VmxonStatus = __vmx_on(&AlignedVmxonRegionPhysicalAddr);
    if (VmxonStatus)
    {
        LogError("Err, executing vmxon instruction failed with status : %d", VmxonStatus);
        return FALSE;
    }

    VCpu->VmxonRegionPhysicalAddress = AlignedVmxonRegionPhysicalAddr;

    //
    // We save the allocated buffer (not the aligned buffer) because we want to free it in vmx termination
    //
    VCpu->VmxonRegionVirtualAddress = VmxonRegion;

    return TRUE;
}

/**
 * @brief Allocate Vmcs region and set the Revision ID based on IA32_VMX_BASIC_MSR
 *
 * @param CurrentGuestState
 * @return BOOLEAN Returns true if allocation was successful and vmptrld executed without error
 * otherwise returns false
 */
_Use_decl_annotations_
BOOLEAN
VmxAllocateVmcsRegion(VIRTUAL_MACHINE_STATE * VCpu)
{
    IA32_VMX_BASIC_REGISTER VmxBasicMsr = {0};
    SIZE_T                  VmcsSize;
    UINT8 *                 VmcsRegion;
    UINT64                  VmcsPhysicalAddr;
    UINT64                  AlignedVmcsRegion;
    UINT64                  AlignedVmcsRegionPhysicalAddr;

#ifdef ENV_WINDOWS
    //
    // at IRQL > DISPATCH_LEVEL memory allocation routines don't work
    //
    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
        KeRaiseIrqlToDpcLevel();
#endif // ENV_WINDOWS

    //
    // Allocating a 4-KByte Contigous Memory region
    //
    VmcsSize   = 2 * VMCS_SIZE;
    VmcsRegion = CrsAllocateContiguousZeroedMemory(VmcsSize + ALIGNMENT_PAGE_SIZE);
    if (VmcsRegion == NULL)
    {
        LogError("Err, couldn't allocate Buffer for VMCS region");
        return FALSE;
    }

    VmcsPhysicalAddr = VirtualAddressToPhysicalAddress(VmcsRegion);

    AlignedVmcsRegion = (BYTE *)((ULONG_PTR)(VmcsRegion + ALIGNMENT_PAGE_SIZE - 1) & ~(ALIGNMENT_PAGE_SIZE - 1));
    LogDebugInfo("VMCS region address : %llx", AlignedVmcsRegion);

    AlignedVmcsRegionPhysicalAddr = (BYTE *)((ULONG_PTR)(VmcsPhysicalAddr + ALIGNMENT_PAGE_SIZE - 1) & ~(ALIGNMENT_PAGE_SIZE - 1));
    LogDebugInfo("VMCS region physical address : %llx", AlignedVmcsRegionPhysicalAddr);

    //
    // get IA32_VMX_BASIC_MSR RevisionId
    //
    VmxBasicMsr.AsUInt = __readmsr(IA32_VMX_BASIC);
    LogDebugInfo("Revision Identifier (IA32_VMX_BASIC - MSR 0x480) : 0x%x", VmxBasicMsr.VmcsRevisionId);

    //
    // Changing Revision Identifier
    //
    *(UINT64 *)AlignedVmcsRegion = VmxBasicMsr.VmcsRevisionId;

    VCpu->VmcsRegionPhysicalAddress = AlignedVmcsRegionPhysicalAddr;
    //
    // We save the allocated buffer (not the aligned buffer)
    // because we want to free it in vmx termination
    //
    VCpu->VmcsRegionVirtualAddress = VmcsRegion;

    return TRUE;
}

/**
 * @brief Allocate VMM Stack
 *
 * @param VCpu The virtual processor's state
 * @return BOOLEAN Returns true if allocation was successful otherwise returns false
 */
BOOLEAN
VmxAllocateVmmStack(_Inout_ VIRTUAL_MACHINE_STATE * VCpu)
{
    //
    // Allocate stack for the VM Exit Handler
    //
    VCpu->VmmStack = ExAllocatePoolWithTag(NonPagedPool, VMM_STACK_SIZE, POOLTAG);
    if (VCpu->VmmStack == NULL)
    {
        LogError("Err, insufficient memory in allocationg vmm stack");
        return FALSE;
    }

    RtlZeroMemory(VCpu->VmmStack, VMM_STACK_SIZE);

    LogDebugInfo("VMM Stack for logical processor : 0x%llx", VCpu->VmmStack);

    return TRUE;
}

/**
 * @brief Allocate a buffer forr Msr Bitmap
 *
 * @param VCpu The virtual processor's state
 * @return BOOLEAN Returns true if allocation was successful otherwise returns false
 */
BOOLEAN
VmxAllocateMsrBitmap(_Inout_ VIRTUAL_MACHINE_STATE * VCpu)
{
    //
    // Allocate memory for MSR Bitmap
    // Should be aligned
    //
    VCpu->MsrBitmapVirtualAddress = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, POOLTAG);
    if (VCpu->MsrBitmapVirtualAddress == NULL)
    {
        LogError("Err, insufficient memory in allocationg MSR Bitmaps");
        return FALSE;
    }

    RtlZeroMemory(VCpu->MsrBitmapVirtualAddress, PAGE_SIZE);
    VCpu->MsrBitmapPhysicalAddress = VirtualAddressToPhysicalAddress(VCpu->MsrBitmapVirtualAddress);

    LogDebugInfo("MSR Bitmap virtual address  : 0x%llx", VCpu->MsrBitmapVirtualAddress);
    LogDebugInfo("MSR Bitmap physical address : 0x%llx", VCpu->MsrBitmapPhysicalAddress);

    return TRUE;
}

/**
 * @brief Allocate a buffer forr I/O Bitmap
 *
 * @param ProcessorID
 * @return BOOLEAN Returns true if allocation was successful otherwise returns false
 */
BOOLEAN
VmxAllocateIoBitmaps(_Inout_ VIRTUAL_MACHINE_STATE * VCpu)
{
    //
    // Allocate memory for I/O Bitmap (A)
    //
    VCpu->IoBitmapVirtualAddressA = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, POOLTAG); // should be aligned
    if (VCpu->IoBitmapVirtualAddressA == NULL)
    {
        LogError("Err, insufficient memory in allocationg I/O Bitmaps A");
        return FALSE;
    }

    RtlZeroMemory(VCpu->IoBitmapVirtualAddressA, PAGE_SIZE);
    VCpu->IoBitmapPhysicalAddressA = VirtualAddressToPhysicalAddress(VCpu->IoBitmapVirtualAddressA);

    LogDebugInfo("I/O Bitmap A Virtual Address  : 0x%llx", VCpu->IoBitmapVirtualAddressA);
    LogDebugInfo("I/O Bitmap A Physical Address : 0x%llx", VCpu->IoBitmapPhysicalAddressA);

    //
    // Allocate memory for I/O Bitmap (B)
    //
    VCpu->IoBitmapVirtualAddressB = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, POOLTAG); // should be aligned
    if (VCpu->IoBitmapVirtualAddressB == NULL)
    {
        LogError("Err, insufficient memory in allocationg I/O Bitmaps B");
        return FALSE;
    }

    RtlZeroMemory(VCpu->IoBitmapVirtualAddressB, PAGE_SIZE);
    VCpu->IoBitmapPhysicalAddressB = VirtualAddressToPhysicalAddress(VCpu->IoBitmapVirtualAddressB);

    LogDebugInfo("I/O Bitmap B virtual address  : 0x%llx", VCpu->IoBitmapVirtualAddressB);
    LogDebugInfo("I/O Bitmap B physical address : 0x%llx", VCpu->IoBitmapPhysicalAddressB);

    return TRUE;
}

/**
 * @brief Allocates a buffer and tests for the MSRs that cause #GP
 *
 * @return UINT64 Allocated buffer for MSR Bitmap
 */
UINT64 *
VmxAllocateInvalidMsrBimap()
{
    UINT64 * InvalidMsrBitmap;

    InvalidMsrBitmap = ExAllocatePoolWithTag(NonPagedPool, 0x1000 / 0x8, POOLTAG);

    if (InvalidMsrBitmap == NULL)
    {
        return NULL;
    }

    RtlZeroMemory(InvalidMsrBitmap, 0x1000 / 0x8);

    for (size_t i = 0; i < 0x1000; ++i)
    {
        __try
        {
            __readmsr(i);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            SetBit(i, InvalidMsrBitmap);
        }
    }

    return InvalidMsrBitmap;
}
