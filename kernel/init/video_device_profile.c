/**
 * @file video_device_profile.c
 * @brief Fail-closed VMware SVGA-II and NVIDIA GK208 ownership profiles.
 *
 * VMware device identifiers follow vm_device_version.h from open-vm-tools.
 * VMware deliberately receives no DMA, IRQ or raw BAR authority.  GK208 may
 * stage data only in one kernel-owned mediated pool and still receives no raw
 * mapping, DMA address, IRQ or bus-master authority.
 */
#include "kernel/init/video_device_profile.h"

#include <stddef.h>

#include "arch/x86/boot/vbe_runtime.h"
#include "drivers/bus/pci.h"
#include "include/kernel/device_domain.h"
#include "lib/libc/string.h"

#define VMWARE_VENDOR_ID 0x15ADU
#define VMWARE_SVGA2_ID 0x0405U
#define VMWARE_SVGA_ID 0x0710U
#define VMWARE_DISPLAY_CLASS 0x03U
#define NVIDIA_VENDOR_ID 0x10DEU
#define NVIDIA_GK208_DEVICE_ID 0x1280U
#define DISPLAY_VGA_SUBCLASS 0x00U
#define NVIDIA_GPC31_TPC_COUNT 0x005FA608U
#define NVIDIA_BAR0_READABLE_BYTES \
    (NVIDIA_GPC31_TPC_COUNT + sizeof(uint32_t))
#define NVIDIA_DMA_USERD_OFFSET 0x00004000U
#define NVIDIA_DMA_RAMFC_OFFSET 0x00005000U
#define NVIDIA_DMA_PGD_OFFSET 0x00010000U
#define NVIDIA_DMA_PGT_OFFSET 0x00030000U
#define NVIDIA_DMA_GPFIFO_OFFSET 0x00001000U
#define NVIDIA_DMA_PUSHBUF_OFFSET 0x00002000U
#define NVIDIA_DMA_FENCE_OFFSET 0x00003000U
#define NVIDIA_RAMFC_USERD_OFFSET 0x00000008U
#define NVIDIA_RAMFC_PGD_OFFSET 0x00000200U
#define NVIDIA_VM_PTE_NCOH 0x0000000600000001ULL
#define NVIDIA_VM_PTE_READ_ONLY (1ULL << 2U)
#define NVIDIA_FB_PAGE_MODE_REGISTER 0x00100C80U
#define NVIDIA_FB_PAGE_MODE_MASK 0x00000001U
#define NVIDIA_PMC_ENABLE_REGISTER 0x00000200U
#define NVIDIA_PMC_ENABLE_GR_MASK 0x00001000U
#define NVIDIA_FECS_BASE 0x00409000U
#define NVIDIA_GPCCS_BASE 0x0041A000U
#define NVIDIA_GR_FIRMWARE_POLICY_ID 1U
#define NVIDIA_FECS_DATA_OFFSET 0x00070000U
#define NVIDIA_FECS_CODE_OFFSET 0x00070400U
#define NVIDIA_GPCCS_DATA_OFFSET 0x00071000U
#define NVIDIA_GPCCS_CODE_OFFSET 0x00071400U
#define NVIDIA_GR_EXECUTION_OFFSET 0x00072000U
#define NVIDIA_GR_EXECUTION_OP_CAPACITY 2048U
#define NVIDIA_GR_EXECUTION_FLAGS 0x00000007U
#define NVIDIA_GR_PREREQUISITE_POLICY_ID 1U
#define NVIDIA_GR_FAULT_BUFFER_BYTES 0x00020000U
#define NVIDIA_GR_FB_PAGE_SHIFT 17U
#define NVIDIA_GR_MAX_FBPS 8U
#define NVIDIA_GR_MAX_FBPAS 16U

static device_domain_dma_relocation_rule_t nvidia_relocation(
        uint32_t destination, uint32_t source, uint32_t shift,
        uint64_t fixed_bits) {
    return (device_domain_dma_relocation_rule_t){
        .destination_pool_offset = destination,
        .source_pool_offset = source,
        .shift_right = shift,
        .width = sizeof(uint64_t),
        .fixed_bits = fixed_bits,
    };
}

static int install_nvidia_dma_relocation_policy(uint32_t device_index) {
    device_domain_dma_relocation_policy_t policy = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(policy),
        .policy_count = 2U,
    };
    static const uint32_t page_shifts[2] = {16U, 17U};
    static const uint32_t pgd_indices[2] = {8U, 4U};
    for (uint32_t variant = 0U; variant < 2U; ++variant) {
        device_domain_dma_relocation_template_t *relocation =
            &policy.policies[variant];
        relocation->policy_id = page_shifts[variant];
        relocation->rule_count = 6U;
        relocation->rules[0] = nvidia_relocation(
            NVIDIA_DMA_RAMFC_OFFSET + NVIDIA_RAMFC_USERD_OFFSET,
            NVIDIA_DMA_USERD_OFFSET, 0U, 0U);
        relocation->rules[1] = nvidia_relocation(
            NVIDIA_DMA_RAMFC_OFFSET + NVIDIA_RAMFC_PGD_OFFSET,
            NVIDIA_DMA_PGD_OFFSET, 0U, 3U);
        relocation->rules[2] = nvidia_relocation(
            NVIDIA_DMA_PGD_OFFSET + pgd_indices[variant] * sizeof(uint64_t),
            NVIDIA_DMA_PGT_OFFSET, 8U, 3U);
        relocation->rules[3] = nvidia_relocation(
            NVIDIA_DMA_PGT_OFFSET, NVIDIA_DMA_PUSHBUF_OFFSET, 8U,
            NVIDIA_VM_PTE_NCOH | NVIDIA_VM_PTE_READ_ONLY);
        relocation->rules[4] = nvidia_relocation(
            NVIDIA_DMA_PGT_OFFSET + sizeof(uint64_t),
            NVIDIA_DMA_FENCE_OFFSET, 8U, NVIDIA_VM_PTE_NCOH);
        relocation->rules[5] = nvidia_relocation(
            NVIDIA_DMA_PGT_OFFSET + 2U * sizeof(uint64_t),
            NVIDIA_DMA_GPFIFO_OFFSET, 8U,
            NVIDIA_VM_PTE_NCOH | NVIDIA_VM_PTE_READ_ONLY);
    }
    return device_domain_install_dma_relocation_policy(device_index, &policy);
}

static int install_nvidia_dma_vm_page_mode_policy(uint32_t device_index) {
    const device_domain_dma_vm_page_mode_policy_t policy = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(policy),
        .policy_count = 2U,
        .policies = {
            {
                .policy_id = 16U,
                .region_index = 0U,
                .register_offset = NVIDIA_FB_PAGE_MODE_REGISTER,
                .width = sizeof(uint32_t),
                .writable_mask = NVIDIA_FB_PAGE_MODE_MASK,
                .value = NVIDIA_FB_PAGE_MODE_MASK,
            },
            {
                .policy_id = 17U,
                .region_index = 0U,
                .register_offset = NVIDIA_FB_PAGE_MODE_REGISTER,
                .width = sizeof(uint32_t),
                .writable_mask = NVIDIA_FB_PAGE_MODE_MASK,
                .value = 0U,
            },
        },
    };
    return device_domain_install_dma_vm_page_mode_policy(
        device_index, &policy);
}

static int install_nvidia_gr_firmware_policy(uint32_t device_index) {
    const device_domain_gr_firmware_policy_t policy = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(policy),
        .policy_id = NVIDIA_GR_FIRMWARE_POLICY_ID,
        .region_index = 0U,
        .pmc_enable_offset = NVIDIA_PMC_ENABLE_REGISTER,
        .pmc_gr_mask = NVIDIA_PMC_ENABLE_GR_MASK,
        .image_count = DEVICE_DOMAIN_GR_FIRMWARE_IMAGE_COUNT,
        .images = {
            {
                .pool_offset = NVIDIA_FECS_DATA_OFFSET,
                .word_count = 193U,
                .crc32 = 0x599287F1U,
                .falcon_base = NVIDIA_FECS_BASE,
                .memory_kind = DEVICE_DOMAIN_GR_FIRMWARE_DMEM,
            },
            {
                .pool_offset = NVIDIA_FECS_CODE_OFFSET,
                .word_count = 640U,
                .crc32 = 0x761F1915U,
                .falcon_base = NVIDIA_FECS_BASE,
                .memory_kind = DEVICE_DOMAIN_GR_FIRMWARE_IMEM,
            },
            {
                .pool_offset = NVIDIA_GPCCS_DATA_OFFSET,
                .word_count = 27U,
                .crc32 = 0xF7976F94U,
                .falcon_base = NVIDIA_GPCCS_BASE,
                .memory_kind = DEVICE_DOMAIN_GR_FIRMWARE_DMEM,
            },
            {
                .pool_offset = NVIDIA_GPCCS_CODE_OFFSET,
                .word_count = 384U,
                .crc32 = 0xF70A347FU,
                .falcon_base = NVIDIA_GPCCS_BASE,
                .memory_kind = DEVICE_DOMAIN_GR_FIRMWARE_IMEM,
            },
        },
    };
    return device_domain_install_gr_firmware_policy(device_index, &policy);
}

static int install_nvidia_gr_prerequisite_policy(
        uint32_t device_index, const pci_device_t *device) {
    vbe_runtime_info_t vbe;
    memcpy(&vbe, (const void *)(uintptr_t)VBE_RUNTIME_INFO_ADDRESS,
           sizeof(vbe));
    if (device == NULL || vbe.magic != VBE_RUNTIME_INFO_MAGIC ||
        vbe.version != VBE_RUNTIME_INFO_VERSION ||
        vbe.struct_size != sizeof(vbe) || vbe.reserved != 0U ||
        vbe.framebuffer_address == 0U || vbe.pitch == 0U ||
        vbe.width == 0U || vbe.height == 0U || vbe.bpp != 32U ||
        vbe.memory_type != 1U || vbe.width > UINT32_MAX / 4U ||
        vbe.pitch < vbe.width * 4U ||
        vbe.height > UINT32_MAX / vbe.pitch)
        return -95;
    const uint32_t scanout_bytes = vbe.pitch * vbe.height;
    const uint64_t scanout_end =
        (uint64_t)vbe.framebuffer_address + scanout_bytes;
    if (scanout_end > UINT32_MAX + 1ULL) return -95;

    pci_bar_info_t selected = {0};
    bool found = false;
    for (uint32_t index = 0U; index < 6U; ++index) {
        pci_bar_info_t bar;
        if (!pci_describe_bar(device, index, &bar)) continue;
        if ((bar.flags & PCI_BAR_INFO_MMIO) == 0U ||
            (bar.flags & PCI_BAR_INFO_PIO) != 0U ||
            bar.base_high != 0U || bar.size_high != 0U ||
            bar.base_low == 0U || bar.size_low == 0U)
            continue;
        const uint64_t bar_end = (uint64_t)bar.base_low + bar.size_low;
        if (vbe.framebuffer_address < bar.base_low ||
            scanout_end > bar_end)
            continue;
        if (found) return -95;
        selected = bar;
        found = true;
    }
    if (!found || selected.index == 0U) return -95;
    const uint32_t scanout_offset =
        vbe.framebuffer_address - selected.base_low;
    const device_domain_gr_prerequisite_policy_t policy = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(policy),
        .policy_id = NVIDIA_GR_PREREQUISITE_POLICY_ID,
        .region_index = 0U,
        .vram_region_index = selected.index,
        .execution_pool_offset = NVIDIA_GR_EXECUTION_OFFSET,
        .execution_max_operations = NVIDIA_GR_EXECUTION_OP_CAPACITY,
        .execution_flags = NVIDIA_GR_EXECUTION_FLAGS,
        .scanout_offset = scanout_offset,
        .scanout_bytes = scanout_bytes,
        .vram_aperture_bytes = selected.size_low,
        .fault_buffer_bytes = NVIDIA_GR_FAULT_BUFFER_BYTES,
        .fault_buffer_alignment = NVIDIA_GR_FAULT_BUFFER_BYTES,
        .fb_page_shift = NVIDIA_GR_FB_PAGE_SHIFT,
        .fbp_max = NVIDIA_GR_MAX_FBPS,
        .fbpa_max = NVIDIA_GR_MAX_FBPAS,
    };
    return device_domain_install_gr_prerequisite_policy(
        device_index, &policy);
}

static int register_profile(const pci_device_t *device, uint32_t backend,
                            video_device_profile_info_t *info) {
    const device_domain_profile_t profile = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(profile),
        .isolation_group = DEVICE_DOMAIN_MAX_GROUPS - 2U,
        .flags = DEVICE_DOMAIN_PROFILE_MEDIATED_IO |
            (backend == VIDEO_DEVICE_BACKEND_NVIDIA_GK208
                ? DEVICE_DOMAIN_PROFILE_MEDIATED_DMA |
                    DEVICE_DOMAIN_PROFILE_LARGE_DMA_POOL : 0U),
        .vendor_id = device->vendor_id,
        .device_id = device->device_id,
        .class_code = device->class_code,
        .subclass_code = device->subclass_code,
        .prog_if = device->prog_if,
    };
    uint32_t device_index = 0U;
    int result = device_domain_register(
        &profile, pci_location(device), &device_index);
    if (result != 0) return result;
    if (backend == VIDEO_DEVICE_BACKEND_NVIDIA_GK208) {
        const device_domain_region_policy_t region_policy = {
            .version = DEVICE_DOMAIN_ABI_VERSION,
            .struct_size = sizeof(region_policy),
            .readable_bytes = {NVIDIA_BAR0_READABLE_BYTES},
            .rule_count = 0U,
        };
        result = device_domain_install_region_policy(
            device_index, &region_policy);
        if (result != 0) return result;
        result = install_nvidia_dma_relocation_policy(device_index);
        if (result != 0) return result;
        result = install_nvidia_dma_vm_page_mode_policy(device_index);
        if (result != 0) return result;
        result = install_nvidia_gr_firmware_policy(device_index);
        if (result != 0) return result;
        result = install_nvidia_gr_prerequisite_policy(
            device_index, device);
        if (result != 0) return result;
    }
    *info = (video_device_profile_info_t){
        .device_index = device_index,
        .pci_location = pci_location(device),
        .backend = backend,
        .vendor_id = device->vendor_id,
        .device_id = device->device_id,
    };
    return 1;
}

int video_device_profile_discover(video_device_profile_info_t *info) {
    if (info == NULL) return -22;
    memset(info, 0, sizeof(*info));
    for (size_t index = 0U; index < pci_device_count; ++index) {
        const pci_device_t *device = &pci_devices[index];
        if (device->vendor_id != VMWARE_VENDOR_ID ||
            (device->device_id != VMWARE_SVGA2_ID &&
             device->device_id != VMWARE_SVGA_ID) ||
            device->class_code != VMWARE_DISPLAY_CLASS ||
            device->owner != PCI_OWNER_UNBOUND)
            continue;
        return register_profile(
            device, VIDEO_DEVICE_BACKEND_VMWARE_SVGA2, info);
    }
    for (size_t index = 0U; index < pci_device_count; ++index) {
        const pci_device_t *device = &pci_devices[index];
        if (device->vendor_id != NVIDIA_VENDOR_ID ||
            device->device_id != NVIDIA_GK208_DEVICE_ID ||
            device->class_code != VMWARE_DISPLAY_CLASS ||
            device->subclass_code != DISPLAY_VGA_SUBCLASS ||
            device->owner != PCI_OWNER_UNBOUND)
            continue;
        return register_profile(
            device, VIDEO_DEVICE_BACKEND_NVIDIA_GK208, info);
    }
    return 0;
}
