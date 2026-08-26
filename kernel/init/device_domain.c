/**
 * @file kernel/init/device_domain.c
 * @brief Fixed-capacity lifecycle core for supervised Ring-3 device drivers.
 */
#include "include/kernel/device_domain.h"

#include "arch/x86/platform/acpi.h"
#include "arch/x86/include/sys.h"
#include "drivers/bus/pci.h"
#include "include/kernel/ipc.h"
#include "lib/libc/string.h"

typedef struct {
    uint32_t address;
    uint32_t count;
    uint32_t pitch;
    uint32_t value;
} reist_nvidia_gk208_gr_tuple_t;
typedef struct {
    reist_nvidia_gk208_gr_tuple_t tuple;
    uint32_t class_id;
} reist_nvidia_gk208_gr_method_tuple_t;
typedef struct {
    uint32_t first_tuple;
    uint32_t tuple_count;
} reist_nvidia_gk208_gr_span_t;
typedef struct {
    uint32_t first_tuple;
    uint32_t tuple_count;
    uint32_t falcon_base;
    uint32_t starstar;
    uint32_t register_base;
} reist_nvidia_gk208_gr_context_span_t;
#include "../../userspace/video/lib/nvidia_gk208_gr_tables.h"
#ifndef REIST_HOST_TEST
#include "arch/x86/mm/paging.h"
#include "drivers/char/io.h"
#include "lib/libc/stdio.h"
#include "kernel/time/pit.h"
#include "kernel/sched/scheduler.h"
#endif

#define GK208_GR_EXECUTION_VERSION 1U
#define GK208_GR_EXECUTION_FLAGS 0x00000007U
#define GK208_GR_PLAN_VERSION 1U
#define GK208_GR_PLAN_STRUCT_BYTES 288U
#define GK208_GR_TOPOLOGY_REGISTER 0x00409604U
#define GK208_GR_GPC_UNIT_BASE 0x00500000U
#define GK208_GR_GPC_UNIT_STRIDE 0x00008000U
#define GK208_GR_GPC_TPC_COUNT_OFFSET 0x00002608U
#define GK208_GR_GPC_PPC_MASK_OFFSET 0x00000C30U
#define GK208_GR_MAX_TPCS_PER_GPC 8U
#define GK208_GR_MAX_TOTAL_TPCS 32U
#define GK208_GR_MAX_ROPS 31U
#define GK208_FB_PART_COUNT 0x00022438U
#define GK208_FB_FBPA_COUNT 0x0002243CU
#define GK208_FB_DISABLE_MASK 0x00022554U
#define GK208_FB_PAGE_CONFIG 0x00100C80U
#define GK208_FB_FBPA_SIZE_BASE 0x0011020CU
#define GK208_FB_FBPA_SIZE_STRIDE 0x00001000U
#define GK208_LTC_SLICE_COUNT 0x0017E8DCU
#define GK208_LTC_TAG_BLOCK_BYTES 0x00006000U
#define GK208_LTC_TAG_MIN_MARGIN 0x00006000U
#define GK208_LTC_TAG_ALIGN_PER_LTC 0x00000800U
#define GK208_LTC_COUNT 0x0017E8D8U
#define GK208_LTC_ACTIVE_COUNT 0x0017E000U
#define GK208_LTC_TAG_BASE 0x0017E8D4U
#define GK208_LTC_PAGE_MODE 0x0017E8C0U
#define GK208_LTC_CBC_START 0x0017E8CCU
#define GK208_LTC_CBC_LIMIT 0x0017E8D0U
#define GK208_LTC_CBC_COMMAND 0x0017E8C8U
#define GK208_LTC_CBC_STATUS_BASE 0x001410C8U
#define GK208_LTC_CBC_STATUS_LTC_STRIDE 0x00002000U
#define GK208_LTC_CBC_STATUS_SLICE_STRIDE 0x00000400U
#define GK208_LTC_CBC_CLEAR_COMMAND 0x00000004U
#define GK208_VRAM_VGA_HEAD_BYTES 0x00040000U
#define GK208_VRAM_VBIOS_TAIL_BYTES 0x00100000U
#define GK208_GR_PAGEPOOL_BYTES 0x00008000U
#define GK208_GR_PAGEPOOL_ALIGNMENT 0x00001000U
#define GK208_GR_BUNDLE_BYTES 0x00003000U
#define GK208_GR_BUNDLE_ALIGNMENT 0x00000100U
#define GK208_GR_ATTRIB_ALIGNMENT 0x00001000U
#define GK208_GR_ATTRIB_STRIDE 0x00000020U
#define GK208_GR_ATTRIB_TOTAL_MAX 0x00000B23U
#define GK208_GR_GOLDEN_CB_RESERVED 0x00080000U
#define GK208_GR_GOLDEN_ALIGNMENT 0x00001000U
#define GK208_GR_TEMP_INSTANCE_BYTES 0x00001000U
#define GK208_GR_TEMP_PGD_BYTES 0x00010000U
#define GK208_GR_TEMP_PGT_BYTES 0x00040000U
#define GK208_GR_GPU_PAGE_SHIFT 12U
#define GK208_GR_GPU_BASE 0x20010000U
#define GK208_GR_GPU_TABLE_BASE 0x20000000U
#define GK208_GR_GPU_TABLE_BYTES 0x08000000U
#define GK208_GR_INSTANCE_PGD 0x00000200U
#define GK208_GR_INSTANCE_VM_LIMIT 0x00000208U
#define GK208_GR_INSTANCE_CONTEXT 0x00000210U
#define GK208_GR_FE_POWER 0x00404170U
#define GK208_GR_FECS_RESET 0x00409614U
#define GK208_GR_FECS_STATUS 0x00409800U
#define GK208_GR_FECS_DATA 0x00409500U
#define GK208_GR_FECS_METHOD 0x00409504U
#define GK208_GR_FECS_CURRENT 0x00409B00U
#define GK208_CHANNEL_ID 1U
#define GK208_CHANNEL_LIMIT 1024U
#define GK208_CHANNEL_USERD_BYTES 0x00001000U
#define GK208_CHANNEL_RUNLIST_BYTES 0x00001000U
#define GK208_CHANNEL_SURFACE_GPU 0x21000000U
#define GK208_CHANNEL_PUSH_GPU 0x20000000U
#define GK208_CHANNEL_FENCE_GPU 0x20001000U
#define GK208_CHANNEL_GPFIFO_GPU 0x20002000U
#define GK208_CHANNEL_GPFIFO_BYTES 0x00001000U
#define GK208_CHANNEL_GPFIFO_ENTRIES (GK208_CHANNEL_GPFIFO_BYTES / 8U)
#define GK208_CHANNEL_PUSH_POOL_OFFSET 0x00002000U
#define GK208_CHANNEL_FENCE_POOL_OFFSET 0x00003000U
#define GK208_CHANNEL_GPFIFO_POOL_OFFSET 0x00001000U
#define GK208_CHANNEL_CAPABILITIES \
    (DEVICE_DOMAIN_GR_2D_CAP_RECT_FILL | \
     DEVICE_DOMAIN_GR_2D_CAP_RECT_COPY | DEVICE_DOMAIN_GR_CHANNEL_READY)
#define GK208_FIFO_PMC_ENABLE 0x00000200U
#define GK208_FIFO_PMC_MASK 0x00000100U
#define GK208_FIFO_PBDMA_ENABLE 0x00000204U
#define GK208_FIFO_PBDMA_CONTROL 0x00002A04U
#define GK208_FIFO_INTR_STATUS 0x00002100U
#define GK208_FIFO_INTR_ENABLE 0x00002140U
#define GK208_FIFO_TOP_BASE 0x00022700U
#define GK208_FIFO_TOP_COUNT 64U
#define GK208_FIFO_RUNLIST_BASE 0x00002270U
#define GK208_FIFO_RUNLIST_SUBMIT 0x00002274U
#define GK208_FIFO_RUNLIST_PENDING_BASE 0x00002284U
#define GK208_FIFO_RUNLIST_BLOCK 0x00002630U
#define GK208_FIFO_CHANNEL_BASE 0x00800000U
#define GK208_FIFO_CHANNEL_STRIDE 8U
#define GK208_FIFO_PBDMA_BASE 0x00040100U
#define GK208_FIFO_PBDMA_STRIDE 0x00002000U
#define GK208_FIFO_USERD_GET 0x00000088U
#define GK208_FIFO_USERD_PUT 0x0000008CU
#define GK208_VM_PTE_NCOH 0x0000000600000001ULL
#define GK208_VM_PTE_READ_ONLY (1ULL << 2U)
#define GK208_2D_CLASS 0x0000902DU
#define GK208_2D_SUBCHANNEL 3U
#define GK208_2D_MAX_WORDS 72U

typedef struct {
    uint8_t registered;
    uint32_t pci_location;
    device_domain_profile_t profile;
    uint32_t state;
    uint32_t generation;
    int32_t owner_pid;
    uint32_t owner_generation;
    uint32_t mode;
    uint8_t irq_bound;
    uint8_t dma_bound;
    uint8_t irq_pic_fallback;
    uint8_t mediated_io_quiesced;
    uint32_t irq_capability;
    uint32_t dma_capability;
    device_domain_resource_handle_t irq_resource;
    device_domain_resource_handle_t dma_resource;
    uint32_t irq_sequence;
    uint32_t irq_pending_count;
    uint8_t irq_notification_sent;
    uint64_t irq_deadline_ms;
    uint64_t irq_window_start_ms;
    uint32_t irq_window_count;
    uint32_t irq_storm_count;
    uint8_t region_policy_installed;
    device_domain_region_policy_t region_policy;
    uint8_t dma_relocation_policy_installed;
    device_domain_dma_relocation_policy_t dma_relocation_policy;
    uint8_t dma_vm_page_mode_policy_installed;
    device_domain_dma_vm_page_mode_policy_t dma_vm_page_mode_policy;
    uint8_t dma_vm_page_mode_active;
    device_domain_region_info_t dma_vm_page_mode_region;
    uint32_t dma_vm_page_mode_offset;
    uint32_t dma_vm_page_mode_width;
    uint32_t dma_vm_page_mode_mask;
    uint32_t dma_vm_page_mode_original_bits;
    uint8_t gr_firmware_policy_installed;
    device_domain_gr_firmware_policy_t gr_firmware_policy;
    uint8_t gr_firmware_active;
    device_domain_region_info_t gr_firmware_region;
    uint32_t gr_firmware_pmc_offset;
    uint32_t gr_firmware_pmc_mask;
    uint32_t gr_firmware_pmc_original_bits;
    uint8_t gr_prerequisite_policy_installed;
    device_domain_gr_prerequisite_policy_t gr_prerequisite_policy;
    uint8_t gr_prerequisite_active;
    uint32_t gr_prerequisite_image_crc;
    uint32_t gr_prerequisite_topology_crc;
    uint32_t gr_prerequisite_mmu_read_offset;
    uint32_t gr_prerequisite_mmu_write_offset;
    uint32_t gr_prerequisite_tag_offset;
    uint32_t gr_prerequisite_tag_bytes;
    uint32_t gr_prerequisite_tag_base;
    uint32_t gr_prerequisite_vram_bytes;
    uint32_t gr_prerequisite_ltc_count;
    uint32_t gr_prerequisite_lts_count;
    uint32_t gr_prerequisite_tag_count;
    uint8_t gr_execution_active;
    uint32_t gr_execution_operation_count;
    uint32_t gr_execution_context_size;
    uint8_t gr_context_memory_active;
    uint32_t gr_context_pagepool_offset;
    uint32_t gr_context_bundle_offset;
    uint32_t gr_context_attrib_offset;
    uint32_t gr_context_attrib_bytes;
    uint32_t gr_context_golden_offset;
    uint32_t gr_context_golden_bytes;
    uint32_t gr_context_total_bytes;
    uint8_t gr_golden_context_active;
    uint32_t gr_golden_context_crc32;
    uint32_t gr_golden_context_retained_bytes;
    uint8_t gr_channel_policy_installed;
    device_domain_gr_channel_policy_t gr_channel_policy;
    uint8_t gr_channel_active;
    uint8_t gr_channel_bus_master_active;
    uint32_t gr_channel_instance_offset;
    uint32_t gr_channel_pgd_offset;
    uint32_t gr_channel_pgt_offset;
    uint32_t gr_channel_userd_offset;
    uint32_t gr_channel_runlist_offset;
    uint32_t gr_channel_private_end;
    uint32_t gr_channel_runlist_id;
    uint32_t gr_channel_pbdma_mask;
    uint32_t gr_channel_original_pbdma_mask;
    uint32_t gr_channel_original_pmc_bits;
    uint32_t gr_channel_fence_sequence;
    uint32_t gr_channel_gpfifo_put;
    device_domain_region_info_t gr_channel_mmio;
    device_domain_region_info_t gr_channel_vram_window;
} device_slot_t;

typedef struct {
    int32_t owner_pid;
    uint32_t owner_generation;
    uint32_t claim_count;
} group_owner_t;

typedef struct {
    uint8_t active;
    uint8_t retired;
    uint32_t generation;
    uint32_t kind;
    uint32_t device_slot;
    uint32_t device_generation;
    int32_t owner_pid;
    uint32_t owner_generation;
    uint32_t platform_capability;
    device_domain_region_info_t region;
} resource_slot_t;

typedef struct {
    uint8_t active;
    uint8_t sealed;
    uint32_t device_slot;
    uint32_t device_generation;
    int32_t owner_pid;
    uint32_t owner_generation;
    uint32_t direction;
    uint32_t capacity;
} dma_pool_slot_t;

static device_slot_t devices[DEVICE_DOMAIN_MAX_DEVICES];
static group_owner_t groups[DEVICE_DOMAIN_MAX_GROUPS];
static resource_slot_t resources[DEVICE_DOMAIN_MAX_RESOURCES];
static dma_pool_slot_t dma_pools[DEVICE_DOMAIN_DMA_POOL_COUNT];
static uint8_t dma_pool_storage[DEVICE_DOMAIN_DMA_POOL_COUNT]
                               [DEVICE_DOMAIN_DMA_LARGE_POOL_BYTES]
    __attribute__((aligned(4096)));
static device_domain_platform_ops_t platform_ops;
static uint32_t device_count;
static bool initialized;
static bool platform_iommu_ready;
static volatile uint32_t operation_busy;
static device_domain_iommu_status_t iommu_status;
static uint32_t irq_line_bindings[PCI_LEGACY_IRQ_COUNT];
static volatile uint32_t pending_irq_lines;
static device_domain_dma_pool_stats_t dma_pool_stats;

static uint64_t dma_pool_physical_address(uint32_t pool_index,
                                          uint32_t offset) {
#ifdef REIST_HOST_TEST
    return 0x10000000ULL +
        (uint64_t)pool_index * DEVICE_DOMAIN_DMA_LARGE_POOL_BYTES + offset;
#else
    return (uint64_t)(uintptr_t)&dma_pool_storage[pool_index][offset];
#endif
}

static void increment_saturating(uint32_t *value) {
    if (value != NULL && *value != UINT32_MAX) ++*value;
}

static bool irq_window_admit(device_slot_t *device, uint64_t now_ms) {
    if (device->irq_window_count == 0U) {
        device->irq_window_start_ms = now_ms;
        device->irq_window_count = 1U;
        return true;
    }
    if (now_ms < device->irq_window_start_ms) {
        increment_saturating(&device->irq_storm_count);
        return false;
    }
    if (now_ms - device->irq_window_start_ms >=
        DEVICE_DOMAIN_IRQ_WINDOW_MS) {
        device->irq_window_start_ms = now_ms;
        device->irq_window_count = 1U;
        return true;
    }
    if (device->irq_window_count >= DEVICE_DOMAIN_IRQ_WINDOW_LIMIT) {
        increment_saturating(&device->irq_storm_count);
        return false;
    }
    ++device->irq_window_count;
    return true;
}

#ifdef REIST_RUNTIME_DEGRADATION_FAULT_INJECTION
bool device_domain_irq_storm_self_test(void) {
    device_slot_t device = {0};
    for (uint32_t irq = 0U; irq < DEVICE_DOMAIN_IRQ_WINDOW_LIMIT; ++irq) {
        if (!irq_window_admit(&device, 100U)) return false;
    }
    if (irq_window_admit(&device, 100U) || device.irq_storm_count != 1U)
        return false;

    device = (device_slot_t){0};
    if (!irq_window_admit(&device, 200U) ||
        irq_window_admit(&device, 199U) || device.irq_storm_count != 1U)
        return false;
    device.irq_storm_count = UINT32_MAX;
    if (irq_window_admit(&device, 198U) ||
        device.irq_storm_count != UINT32_MAX)
        return false;
    return true;
}
#endif

#ifdef REIST_DRIVER_DOMAIN_FAULT_INJECTION
#define DEVICE_DOMAIN_FAULT_RECOVERY_LOCATION 0xFFFFFF00U
#define DEVICE_DOMAIN_FAULT_RESET_LOCATION 0xFFFFFF01U
#define DEVICE_DOMAIN_FAULT_VENDOR 0x1AF4U
#define DEVICE_DOMAIN_FAULT_RECOVERY_DEVICE 0xF100U
#define DEVICE_DOMAIN_FAULT_RESET_DEVICE 0xF101U

static bool fault_test_location(uint32_t location) {
    return location == DEVICE_DOMAIN_FAULT_RECOVERY_LOCATION ||
        location == DEVICE_DOMAIN_FAULT_RESET_LOCATION;
}
#endif

static void device_domain_irq_capture(Registers *registers) {
    if (registers == NULL || registers->irq_number < 32U ||
        registers->irq_number >= 32U + PCI_LEGACY_IRQ_COUNT) return;
    uint8_t irq = (uint8_t)(registers->irq_number - 32U);
    (void)irq_pic_mask_line(irq);
    (void)__sync_fetch_and_or(&pending_irq_lines, 1U << irq);
}

static uint64_t platform_monotonic_ms(void) {
#ifdef REIST_HOST_TEST
    return 0U;
#else
    return pit_monotonic_ms();
#endif
}

static bool begin_operation(void) {
    return __sync_lock_test_and_set(&operation_busy, 1U) == 0U;
}

static void end_operation(void) {
    __sync_lock_release(&operation_busy);
}

static bool pci_bus_master(uint32_t location, bool enabled) {
#ifdef REIST_DRIVER_DOMAIN_FAULT_INJECTION
    if (fault_test_location(location)) {
        (void)enabled;
        return true;
    }
#endif
    const pci_device_t *device = pci_find_location(location);
    return device != NULL && pci_set_bus_master_verified(device, enabled);
}

static bool pci_claim_device(uint32_t location,
                             const device_domain_profile_t *profile) {
#ifdef REIST_DRIVER_DOMAIN_FAULT_INJECTION
    if (profile != NULL && fault_test_location(location)) {
        uint16_t expected = location == DEVICE_DOMAIN_FAULT_RECOVERY_LOCATION
            ? DEVICE_DOMAIN_FAULT_RECOVERY_DEVICE
            : DEVICE_DOMAIN_FAULT_RESET_DEVICE;
        return profile->vendor_id == DEVICE_DOMAIN_FAULT_VENDOR &&
            profile->device_id == expected && profile->class_code == 0xFFU;
    }
#endif
    return profile != NULL && pci_claim_for_driver_domain(
        location, profile->vendor_id, profile->device_id,
        profile->class_code, profile->subclass_code, profile->prog_if);
}

static bool pci_mask_intx(uint32_t location) {
#ifdef REIST_DRIVER_DOMAIN_FAULT_INJECTION
    if (fault_test_location(location)) return true;
#endif
    const pci_device_t *device = pci_find_location(location);
    return device != NULL && pci_set_intx_disabled_verified(device, true);
}

static bool pci_unmask_intx(uint32_t location) {
#ifdef REIST_DRIVER_DOMAIN_FAULT_INJECTION
    if (fault_test_location(location)) return true;
#endif
    const pci_device_t *device = pci_find_location(location);
    return device != NULL && pci_set_intx_disabled_verified(device, false);
}

static bool pci_describe_region(uint32_t location,
        uint32_t region_index, device_domain_region_info_t *region) {
    const pci_device_t *device = pci_find_location(location);
    pci_bar_info_t bar;
    if (device == NULL || region == NULL ||
        !pci_describe_bar(device, region_index, &bar)) return false;
    uint32_t flags = 0U;
    if ((bar.flags & PCI_BAR_INFO_MMIO) != 0U)
        flags |= DEVICE_DOMAIN_REGION_MMIO;
    if ((bar.flags & PCI_BAR_INFO_PIO) != 0U)
        flags |= DEVICE_DOMAIN_REGION_PIO;
    if ((bar.flags & PCI_BAR_INFO_64BIT) != 0U)
        flags |= DEVICE_DOMAIN_REGION_64BIT;
    if ((bar.flags & PCI_BAR_INFO_PREFETCHABLE) != 0U)
        flags |= DEVICE_DOMAIN_REGION_PREFETCHABLE;
    *region = (device_domain_region_info_t){
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(*region),
        .region_index = region_index,
        .flags = flags,
        .base_low = bar.base_low,
        .base_high = bar.base_high,
        .length_low = bar.size_low,
        .length_high = bar.size_high,
    };
    return true;
}

static bool region_bounds_valid(const device_domain_region_info_t *region,
                                uint32_t offset, uint32_t width) {
    if (region == NULL || (width != 1U && width != 2U && width != 4U) ||
        (offset & (width - 1U)) != 0U || region->length_high != 0U ||
        region->length_low == 0U ||
        region->length_low > DEVICE_DOMAIN_MAX_REGION_BYTES ||
        offset > region->length_low || width > region->length_low - offset)
        return false;
    bool mmio = (region->flags & DEVICE_DOMAIN_REGION_MMIO) != 0U;
    bool pio = (region->flags & DEVICE_DOMAIN_REGION_PIO) != 0U;
    return mmio != pio;
}

static bool pci_prepare_region(const device_domain_region_info_t *region) {
    if (!region_bounds_valid(region, 0U, 1U) || region->base_high != 0U)
        return false;
#ifdef REIST_HOST_TEST
    return false;
#else
    if ((region->flags & DEVICE_DOMAIN_REGION_MMIO) != 0U)
        return map_kernel_mmio(region->base_low, region->length_low) != NULL;
    return region->base_low <= UINT16_MAX &&
        region->length_low <= (uint32_t)UINT16_MAX + 1U - region->base_low;
#endif
}

static bool pci_read_region(const device_domain_region_info_t *region,
                            uint32_t offset, uint32_t width,
                            uint32_t *value) {
    if (value == NULL || !region_bounds_valid(region, offset, width))
        return false;
#ifdef REIST_HOST_TEST
    return false;
#else
    if ((region->flags & DEVICE_DOMAIN_REGION_MMIO) != 0U) {
        volatile uint8_t *address =
            (volatile uint8_t *)(uintptr_t)(region->base_low + offset);
        if (width == 1U) *value = *address;
        else if (width == 2U) *value = *(volatile uint16_t *)address;
        else *value = *(volatile uint32_t *)address;
        return true;
    }
    uint16_t port = (uint16_t)(region->base_low + offset);
    *value = width == 1U ? inb(port) : width == 2U ? inw(port) : inl(port);
    return true;
#endif
}

static bool pci_write_region(const device_domain_region_info_t *region,
                             uint32_t offset, uint32_t width, uint32_t value) {
    if (!region_bounds_valid(region, offset, width)) return false;
#ifdef REIST_HOST_TEST
    (void)value;
    return false;
#else
    if ((region->flags & DEVICE_DOMAIN_REGION_MMIO) != 0U) {
        volatile uint8_t *address =
            (volatile uint8_t *)(uintptr_t)(region->base_low + offset);
        if (width == 1U) *address = (uint8_t)value;
        else if (width == 2U) *(volatile uint16_t *)address = (uint16_t)value;
        else *(volatile uint32_t *)address = value;
        __asm__ volatile("" ::: "memory");
        return true;
    }
    uint16_t port = (uint16_t)(region->base_low + offset);
    if (width == 1U) outb(port, (uint8_t)value);
    else if (width == 2U) outw(port, (uint16_t)value);
    else outl(port, value);
    return true;
#endif
}

static bool pci_write_dma_address(const device_domain_region_info_t *region,
                                  uint32_t offset, uint32_t address_low,
                                  uint32_t address_high) {
    return region != NULL &&
        (region->flags & DEVICE_DOMAIN_REGION_MMIO) != 0U &&
        offset <= UINT32_MAX - 4U &&
        pci_write_region(region, offset + 4U, 4U, address_high) &&
        pci_write_region(region, offset, 4U, address_low);
}

static bool pci_bind_irq(uint32_t location, int pid,
        uint32_t process_generation, uint32_t capability) {
    const pci_device_t *device = pci_find_location(location);
    if (device == NULL || !pci_irq_is_valid(device->irq_line) ||
        ipc_capability_validate_owner(pid, process_generation, capability,
            IPC_RIGHT_RECEIVE | IPC_RIGHT_CONTROL) != 0) return false;
    uint8_t irq = device->irq_line;
    if (irq_line_bindings[irq] == UINT32_MAX) return false;
    if (irq_line_bindings[irq] == 0U &&
        register_interrupt_handler(irq, device_domain_irq_capture) != 0)
        return false;
    ++irq_line_bindings[irq];
    return true;
}

static bool pci_revoke_irq(uint32_t location, int pid,
        uint32_t process_generation, uint32_t capability) {
    (void)pid;
    (void)process_generation;
    (void)capability;
    const pci_device_t *device = pci_find_location(location);
    if (device == NULL || !pci_irq_is_valid(device->irq_line)) return false;
    bool pic_fallback_allowed = false;
    for (uint32_t index = 0U; index < device_count; ++index) {
        if (devices[index].registered != 0U &&
            devices[index].pci_location == location &&
            devices[index].irq_pic_fallback != 0U) {
            pic_fallback_allowed = true;
            break;
        }
    }
    bool masked = pci_mask_intx(location);
    if (!masked && pic_fallback_allowed)
        masked = irq_pic_mask_line(device->irq_line);
    uint8_t irq = device->irq_line;
    if (irq_line_bindings[irq] == 0U) return masked;
    if (irq_line_bindings[irq] > 1U) {
        --irq_line_bindings[irq];
        return masked;
    }
    if (unregister_interrupt_handler(irq, device_domain_irq_capture) != 0)
        return false;
    irq_line_bindings[irq] = 0U;
    return masked;
}

static bool pci_bind_mediated_dma(uint32_t location, int pid,
        uint32_t process_generation, uint32_t mode, uint32_t capability) {
    const pci_device_t *device = pci_find_location(location);
    return device != NULL && pid > 0 && process_generation != 0U &&
        mode == DEVICE_DOMAIN_MODE_MEDIATED && capability > 0U &&
        capability <= DEVICE_DOMAIN_DMA_POOL_COUNT &&
        pci_set_bus_master_verified(device, false);
}

static bool revoke_dma_fail_closed(uint32_t location, int pid,
        uint32_t process_generation, uint32_t capability) {
    (void)pid;
    (void)process_generation;
    (void)capability;
    return pci_bus_master(location, false);
}

static bool pci_generic_reset(uint32_t location, uint64_t deadline_ms) {
#ifdef REIST_DRIVER_DOMAIN_FAULT_INJECTION
    if (location == DEVICE_DOMAIN_FAULT_RECOVERY_LOCATION)
        return platform_monotonic_ms() < deadline_ms;
    if (location == DEVICE_DOMAIN_FAULT_RESET_LOCATION) return false;
#endif
    const pci_device_t *device = pci_find_location(location);
    return device != NULL && pci_function_reset_verified(device, deadline_ms);
}

bool device_domain_bootstrap(void) {
    const device_domain_platform_ops_t ops = {
        .monotonic_ms = platform_monotonic_ms,
        .claim_device = pci_claim_device,
        .set_bus_master = pci_bus_master,
        .mask_irq = pci_mask_intx,
        .unmask_irq = pci_unmask_intx,
        .describe_region = pci_describe_region,
        .prepare_region = pci_prepare_region,
        .read_region = pci_read_region,
        .write_region = pci_write_region,
        .write_dma_address = pci_write_dma_address,
        .bind_irq = pci_bind_irq,
        .revoke_irq = pci_revoke_irq,
        .bind_dma = pci_bind_mediated_dma,
        .revoke_dma = revoke_dma_fail_closed,
        .reset = pci_generic_reset,
    };
    /* The legacy ACPI scanner dereferences the BIOS data area and firmware
     * tables through physical identity addresses.  The null page is already
     * deliberately absent at this point, so invoking it here would turn a
     * diagnostic inventory into a kernel page fault.  Until boot publishes a
     * safe physical-memory ACPI snapshot, report no IOMMU and retain fully
     * mediated DMA.  Never weaken the null-page guard for diagnostics. */
    x86_acpi_iommu_inventory_t inventory = {0};
    /* A valid DMAR table is inventory, not proof that translation has been
     * enabled or that every requester belongs to a validated isolation group.
     * Direct assignment therefore remains disabled. */
    if (!device_domain_init(&ops, false)) return false;
    iommu_status.firmware_root_valid = inventory.acpi_root_valid;
    iommu_status.dmar_present = inventory.dmar_present;
    iommu_status.dmar_valid = inventory.dmar_valid;
    iommu_status.remapping_unit_count = inventory.remapping_unit_count;
    iommu_status.interrupt_remapping_reported =
        inventory.interrupt_remapping_reported;
    return true;
}

#ifdef REIST_DRIVER_DOMAIN_FAULT_INJECTION
int device_domain_fault_test_register(uint32_t *recovery_device_out,
                                      uint32_t *reset_device_out) {
    if (recovery_device_out == NULL || reset_device_out == NULL) return -22;
    const device_domain_profile_t recovery = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(recovery),
        .isolation_group = 1U,
        .flags = DEVICE_DOMAIN_PROFILE_MEDIATED_DMA,
        .vendor_id = DEVICE_DOMAIN_FAULT_VENDOR,
        .device_id = DEVICE_DOMAIN_FAULT_RECOVERY_DEVICE,
        .class_code = 0xFFU,
    };
    int status = device_domain_register(
        &recovery, DEVICE_DOMAIN_FAULT_RECOVERY_LOCATION,
        recovery_device_out);
    if (status != 0) return status;
    const device_domain_profile_t reset_failure = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(reset_failure),
        .isolation_group = 2U,
        .flags = DEVICE_DOMAIN_PROFILE_MEDIATED_DMA,
        .vendor_id = DEVICE_DOMAIN_FAULT_VENDOR,
        .device_id = DEVICE_DOMAIN_FAULT_RESET_DEVICE,
        .class_code = 0xFFU,
    };
    return device_domain_register(
        &reset_failure, DEVICE_DOMAIN_FAULT_RESET_LOCATION,
        reset_device_out);
}
#endif

static device_domain_handle_t make_handle(uint32_t slot, uint32_t generation) {
    return (generation << 8U) | (slot + 1U);
}

static bool decode_handle(device_domain_handle_t handle, uint32_t *slot_out,
                          uint32_t *generation_out) {
    uint32_t encoded_slot = handle & 0xFFU;
    uint32_t generation = handle >> 8U;
    if (encoded_slot == 0U || encoded_slot > DEVICE_DOMAIN_MAX_DEVICES ||
        generation == 0U || slot_out == NULL || generation_out == NULL)
        return false;
    *slot_out = encoded_slot - 1U;
    *generation_out = generation;
    return true;
}

static device_domain_resource_handle_t make_resource_handle(
        uint32_t slot, uint32_t generation) {
    return (generation << 8U) | (slot + 1U);
}

static bool decode_resource_handle(device_domain_resource_handle_t handle,
                                   uint32_t *slot_out,
                                   uint32_t *generation_out) {
    uint32_t encoded_slot = handle & 0xFFU;
    uint32_t generation = handle >> 8U;
    if (encoded_slot == 0U ||
        encoded_slot > DEVICE_DOMAIN_MAX_RESOURCES || generation == 0U ||
        slot_out == NULL || generation_out == NULL) return false;
    *slot_out = encoded_slot - 1U;
    *generation_out = generation;
    return true;
}

static int available_resource_slot(void) {
    for (uint32_t index = 0U; index < DEVICE_DOMAIN_MAX_RESOURCES; ++index)
        if (resources[index].active == 0U && resources[index].retired == 0U)
            return (int)index;
    return -1;
}

static int available_dma_pool(void) {
    for (uint32_t index = 0U; index < DEVICE_DOMAIN_DMA_POOL_COUNT; ++index)
        if (dma_pools[index].active == 0U) return (int)index;
    return -1;
}

static bool resource_exists(uint32_t device_index, uint32_t device_generation,
                            uint32_t kind, uint32_t platform_capability) {
    for (uint32_t index = 0U; index < DEVICE_DOMAIN_MAX_RESOURCES; ++index) {
        const resource_slot_t *resource = &resources[index];
        if (resource->active != 0U && resource->device_slot == device_index &&
            resource->device_generation == device_generation &&
            resource->kind == kind &&
            resource->platform_capability == platform_capability) return true;
    }
    return false;
}

static device_domain_resource_handle_t publish_resource(
        uint32_t resource_index, uint32_t kind, uint32_t device_index,
        const device_slot_t *device, uint32_t platform_capability) {
    resource_slot_t *resource = &resources[resource_index];
    if (resource->generation == 0U) resource->generation = 1U;
    resource->active = 1U;
    resource->kind = kind;
    resource->device_slot = device_index;
    resource->device_generation = device->generation;
    resource->owner_pid = device->owner_pid;
    resource->owner_generation = device->owner_generation;
    resource->platform_capability = platform_capability;
    return make_resource_handle(resource_index, resource->generation);
}

static void retire_resource(resource_slot_t *resource) {
    if (resource == NULL || resource->active == 0U) return;
    uint32_t generation = resource->generation;
    uint8_t retired = generation == DEVICE_DOMAIN_HANDLE_GENERATION_MAX
        ? 1U : 0U;
    if (retired == 0U) ++generation;
    *resource = (resource_slot_t){
        .retired = retired,
        .generation = generation,
    };
}

static void retire_device_resources(uint32_t device_index,
                                    uint32_t device_generation) {
    for (uint32_t index = 0U; index < DEVICE_DOMAIN_MAX_RESOURCES; ++index) {
        resource_slot_t *resource = &resources[index];
        if (resource->active != 0U &&
            resource->device_slot == device_index &&
            resource->device_generation == device_generation)
            retire_resource(resource);
    }
}

static bool profile_valid(const device_domain_profile_t *profile) {
    const uint32_t known_flags = DEVICE_DOMAIN_PROFILE_MEDIATED_DMA |
        DEVICE_DOMAIN_PROFILE_IOMMU_DIRECT |
        DEVICE_DOMAIN_PROFILE_GROUP_ISOLATED |
        DEVICE_DOMAIN_PROFILE_LEGACY_INTX_PIC |
        DEVICE_DOMAIN_PROFILE_MEDIATED_IO |
        DEVICE_DOMAIN_PROFILE_LARGE_DMA_POOL;
    return profile != NULL && profile->version == DEVICE_DOMAIN_ABI_VERSION &&
        profile->struct_size == sizeof(*profile) &&
        profile->isolation_group < DEVICE_DOMAIN_MAX_GROUPS &&
        profile->isolation_group != 0U && profile->vendor_id != 0U &&
        profile->vendor_id != 0xFFFFU && profile->device_id != 0xFFFFU &&
        (profile->flags & ~known_flags) == 0U &&
        ((profile->flags & DEVICE_DOMAIN_PROFILE_LEGACY_INTX_PIC) == 0U ||
         (profile->flags & DEVICE_DOMAIN_PROFILE_MEDIATED_DMA) != 0U) &&
        ((profile->flags & DEVICE_DOMAIN_PROFILE_LARGE_DMA_POOL) == 0U ||
         (profile->flags & DEVICE_DOMAIN_PROFILE_MEDIATED_DMA) != 0U) &&
        profile->reserved_byte == 0U && profile->reserved[0] == 0U &&
        profile->reserved[1] == 0U && profile->reserved[2] == 0U;
}

static device_slot_t *owned_slot(int pid, uint32_t process_generation,
                                 device_domain_handle_t handle) {
    uint32_t slot = 0U;
    uint32_t generation = 0U;
    if (pid <= 0 || process_generation == 0U ||
        !decode_handle(handle, &slot, &generation)) return NULL;
    device_slot_t *device = &devices[slot];
    if (slot >= device_count || device->registered == 0U ||
        device->generation != generation || device->owner_pid != pid ||
        device->owner_generation != process_generation) return NULL;
    return device;
}

static resource_slot_t *owned_resource_locked(
        int pid, uint32_t process_generation,
        device_domain_resource_handle_t handle, uint32_t kind) {
    uint32_t slot = 0U;
    uint32_t generation = 0U;
    if (!decode_resource_handle(handle, &slot, &generation)) return NULL;
    resource_slot_t *resource = &resources[slot];
    if (resource->active == 0U || resource->generation != generation ||
        resource->kind != kind || resource->owner_pid != pid ||
        resource->owner_generation != process_generation ||
        resource->device_slot >= device_count) return NULL;
    device_slot_t *device = &devices[resource->device_slot];
    if (device->generation != resource->device_generation ||
        device->owner_pid != pid ||
        device->owner_generation != process_generation) return NULL;
    return resource;
}

static void release_mediated_dma_pool(device_slot_t *device) {
    if (device == NULL || device->mode != DEVICE_DOMAIN_MODE_MEDIATED ||
        device->dma_capability == 0U ||
        device->dma_capability > DEVICE_DOMAIN_DMA_POOL_COUNT) return;
    uint32_t index = device->dma_capability - 1U;
    dma_pool_slot_t *pool = &dma_pools[index];
    if (pool->active != 0U && pool->device_slot == (uint32_t)(device - devices) &&
        pool->device_generation == device->generation &&
        pool->owner_pid == device->owner_pid &&
        pool->owner_generation == device->owner_generation) {
        uint32_t capacity = pool->capacity;
        if (capacity != DEVICE_DOMAIN_DMA_POOL_BYTES &&
            capacity != DEVICE_DOMAIN_DMA_LARGE_POOL_BYTES)
            capacity = DEVICE_DOMAIN_DMA_LARGE_POOL_BYTES;
        memset(dma_pool_storage[index], 0, capacity);
        *pool = (dma_pool_slot_t){0};
        if (dma_pool_stats.active_pools != 0U)
            --dma_pool_stats.active_pools;
    }
}

static bool mode_allowed(const device_slot_t *device, uint32_t mode) {
    if (mode == DEVICE_DOMAIN_MODE_MEDIATED)
        return (device->profile.flags & (DEVICE_DOMAIN_PROFILE_MEDIATED_DMA |
                                         DEVICE_DOMAIN_PROFILE_MEDIATED_IO)) != 0U;
    if (mode != DEVICE_DOMAIN_MODE_IOMMU_DIRECT) return false;
    return platform_iommu_ready &&
        (device->profile.flags & DEVICE_DOMAIN_PROFILE_IOMMU_DIRECT) != 0U &&
        (device->profile.flags & DEVICE_DOMAIN_PROFILE_GROUP_ISOLATED) != 0U;
}

static bool pic_mask_location(uint32_t pci_location) {
    const pci_device_t *device = pci_find_location(pci_location);
    return device != NULL && pci_irq_is_valid(device->irq_line) &&
        irq_pic_mask_line(device->irq_line);
}

static bool pic_unmask_location(uint32_t pci_location) {
    const pci_device_t *device = pci_find_location(pci_location);
    return device != NULL && pci_irq_is_valid(device->irq_line) &&
        irq_pic_unmask_line(device->irq_line);
}

static bool mask_device_irq(const device_slot_t *device) {
    if (device == NULL) return false;
    return device->irq_pic_fallback != 0U
        ? pic_mask_location(device->pci_location)
        : platform_ops.mask_irq(device->pci_location);
}

static bool profile_is_irqless_mediated_io(
        const device_domain_profile_t *profile) {
    return profile != NULL &&
        (profile->flags & DEVICE_DOMAIN_PROFILE_MEDIATED_IO) != 0U &&
        (profile->flags & (DEVICE_DOMAIN_PROFILE_IOMMU_DIRECT |
                           DEVICE_DOMAIN_PROFILE_LEGACY_INTX_PIC)) == 0U;
}

static bool device_is_passive_mediated_io(const device_slot_t *device) {
    return device != NULL &&
        profile_is_irqless_mediated_io(&device->profile) &&
        device->state != DEVICE_DOMAIN_ACTIVE && device->irq_bound == 0U;
}

static bool unmask_device_irq(const device_slot_t *device) {
    if (device == NULL) return false;
    if (device->irq_pic_fallback == 0U)
        return platform_ops.unmask_irq(device->pci_location);
    /* Clear INTx-disable if the endpoint implements it, then release the
     * line-level mask selected by the immutable profile. */
    return platform_ops.unmask_irq(device->pci_location) &&
        pic_unmask_location(device->pci_location);
}

static bool restore_dma_vm_page_mode(device_slot_t *device) {
    if (device == NULL || device->dma_vm_page_mode_active == 0U) return true;
    uint32_t current = 0U;
    if (!platform_ops.read_region(
            &device->dma_vm_page_mode_region,
            device->dma_vm_page_mode_offset,
            device->dma_vm_page_mode_width, &current))
        return false;
    const uint32_t preserved = current & ~device->dma_vm_page_mode_mask;
    const uint32_t restored = preserved |
        device->dma_vm_page_mode_original_bits;
    uint32_t verified = 0U;
    if (!platform_ops.write_region(
            &device->dma_vm_page_mode_region,
            device->dma_vm_page_mode_offset,
            device->dma_vm_page_mode_width, restored) ||
        !platform_ops.read_region(
            &device->dma_vm_page_mode_region,
            device->dma_vm_page_mode_offset,
            device->dma_vm_page_mode_width, &verified) ||
        (verified & device->dma_vm_page_mode_mask) !=
            device->dma_vm_page_mode_original_bits ||
        (verified & ~device->dma_vm_page_mode_mask) != preserved)
        return false;
    device->dma_vm_page_mode_active = 0U;
    device->dma_vm_page_mode_region = (device_domain_region_info_t){0};
    device->dma_vm_page_mode_offset = 0U;
    device->dma_vm_page_mode_width = 0U;
    device->dma_vm_page_mode_mask = 0U;
    device->dma_vm_page_mode_original_bits = 0U;
    return true;
}

static bool toggle_gr_reset(const device_domain_region_info_t *region,
                            uint32_t pmc_offset, uint32_t pmc_mask,
                            uint32_t restore_bits) {
    uint32_t current = 0U;
    if (region == NULL || pmc_mask == 0U ||
        !platform_ops.read_region(
            region, pmc_offset, sizeof(uint32_t), &current))
        return false;
    const uint32_t preserved = current & ~pmc_mask;
    const uint32_t disabled = preserved;
    const uint32_t restored = preserved | restore_bits;
    uint32_t verified = 0U;
    if (!platform_ops.write_region(
            region, pmc_offset, sizeof(uint32_t), disabled) ||
        !platform_ops.read_region(
            region, pmc_offset, sizeof(uint32_t), &verified) ||
        verified != disabled ||
        !platform_ops.write_region(
            region, pmc_offset, sizeof(uint32_t), restored) ||
        !platform_ops.read_region(
            region, pmc_offset, sizeof(uint32_t), &verified) ||
        verified != restored)
        return false;
    return true;
}

static bool reset_gr_firmware_state(device_slot_t *device) {
    if (device == NULL || device->gr_firmware_active == 0U) return true;
    if (!toggle_gr_reset(&device->gr_firmware_region,
            device->gr_firmware_pmc_offset,
            device->gr_firmware_pmc_mask,
            device->gr_firmware_pmc_original_bits))
        return false;
    device->gr_firmware_active = 0U;
    device->gr_firmware_region = (device_domain_region_info_t){0};
    device->gr_firmware_pmc_offset = 0U;
    device->gr_firmware_pmc_mask = 0U;
    device->gr_firmware_pmc_original_bits = 0U;
    return true;
}

static bool gr_channel_hardware_stop(device_slot_t *device);
static bool gr_channel_scrub_private(device_slot_t *device);

static void clear_gr_channel_state(device_slot_t *device) {
    if (device == NULL) return;
    device->gr_channel_active = 0U;
    device->gr_channel_bus_master_active = 0U;
    device->gr_channel_instance_offset = 0U;
    device->gr_channel_pgd_offset = 0U;
    device->gr_channel_pgt_offset = 0U;
    device->gr_channel_userd_offset = 0U;
    device->gr_channel_runlist_offset = 0U;
    device->gr_channel_private_end = 0U;
    device->gr_channel_runlist_id = 0U;
    device->gr_channel_pbdma_mask = 0U;
    device->gr_channel_original_pbdma_mask = 0U;
    device->gr_channel_original_pmc_bits = 0U;
    device->gr_channel_fence_sequence = 0U;
    device->gr_channel_gpfifo_put = 0U;
    device->gr_channel_mmio = (device_domain_region_info_t){0};
    device->gr_channel_vram_window = (device_domain_region_info_t){0};
}

static void clear_gr_prerequisite_state(device_slot_t *device) {
    if (device == NULL) return;
    device->gr_prerequisite_active = 0U;
    device->gr_prerequisite_image_crc = 0U;
    device->gr_prerequisite_topology_crc = 0U;
    device->gr_prerequisite_mmu_read_offset = 0U;
    device->gr_prerequisite_mmu_write_offset = 0U;
    device->gr_prerequisite_tag_offset = 0U;
    device->gr_prerequisite_tag_bytes = 0U;
    device->gr_prerequisite_tag_base = 0U;
    device->gr_prerequisite_vram_bytes = 0U;
    device->gr_prerequisite_ltc_count = 0U;
    device->gr_prerequisite_lts_count = 0U;
    device->gr_prerequisite_tag_count = 0U;
}

static void clear_gr_execution_state(device_slot_t *device) {
    if (device == NULL) return;
    device->gr_context_memory_active = 0U;
    device->gr_context_pagepool_offset = 0U;
    device->gr_context_bundle_offset = 0U;
    device->gr_context_attrib_offset = 0U;
    device->gr_context_attrib_bytes = 0U;
    device->gr_context_golden_offset = 0U;
    device->gr_context_golden_bytes = 0U;
    device->gr_context_total_bytes = 0U;
    device->gr_golden_context_active = 0U;
    device->gr_golden_context_crc32 = 0U;
    device->gr_golden_context_retained_bytes = 0U;
    device->gr_execution_active = 0U;
    device->gr_execution_operation_count = 0U;
    device->gr_execution_context_size = 0U;
}

static bool fence_slot(device_slot_t *device) {
    bool irq_masked = device_is_passive_mediated_io(device) ||
        mask_device_irq(device);
    bool channel_stopped = gr_channel_hardware_stop(device);
    bool mastering_disabled =
        platform_ops.set_bus_master(device->pci_location, false);
    bool channel_scrubbed = mastering_disabled &&
        gr_channel_scrub_private(device);
    if (mastering_disabled) clear_gr_channel_state(device);
    bool gr_firmware_reset = mastering_disabled &&
        reset_gr_firmware_state(device);
    if (gr_firmware_reset) clear_gr_execution_state(device);
    bool page_mode_restored = gr_firmware_reset &&
        restore_dma_vm_page_mode(device);
    if (gr_firmware_reset) clear_gr_prerequisite_state(device);
    bool irq_revoked = device->irq_bound == 0U || platform_ops.revoke_irq(
        device->pci_location, device->owner_pid, device->owner_generation,
        device->irq_capability);
    bool dma_revoked = device->dma_bound == 0U || platform_ops.revoke_dma(
        device->pci_location, device->owner_pid, device->owner_generation,
        device->dma_capability);
    release_mediated_dma_pool(device);
    device->irq_bound = 0U;
    device->dma_bound = 0U;
    device->irq_capability = 0U;
    device->dma_capability = 0U;
    device->irq_resource = 0U;
    device->dma_resource = 0U;
    device->irq_pending_count = 0U;
    device->irq_notification_sent = 0U;
    device->irq_deadline_ms = 0U;
    device->irq_window_start_ms = 0U;
    device->irq_window_count = 0U;
    retire_device_resources((uint32_t)(device - devices), device->generation);
    device->state = DEVICE_DOMAIN_FENCED;
    return irq_masked && channel_stopped && mastering_disabled &&
        channel_scrubbed && gr_firmware_reset && page_mode_restored &&
        irq_revoked && dma_revoked;
}

bool device_domain_init(const device_domain_platform_ops_t *ops,
                        bool iommu_ready) {
    if (ops == NULL || ops->monotonic_ms == NULL ||
        ops->claim_device == NULL ||
        ops->set_bus_master == NULL || ops->mask_irq == NULL ||
        ops->unmask_irq == NULL ||
        ops->describe_region == NULL || ops->prepare_region == NULL ||
        ops->read_region == NULL || ops->write_region == NULL ||
        ops->write_dma_address == NULL ||
        ops->bind_irq == NULL || ops->revoke_irq == NULL ||
        ops->bind_dma == NULL || ops->revoke_dma == NULL ||
        ops->reset == NULL) return false;
    if (initialized) return false;
    memset(devices, 0, sizeof(devices));
    memset(groups, 0, sizeof(groups));
    memset(resources, 0, sizeof(resources));
    memset(dma_pools, 0, sizeof(dma_pools));
    memset(dma_pool_storage, 0, sizeof(dma_pool_storage));
    dma_pool_stats = (device_domain_dma_pool_stats_t){
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(dma_pool_stats),
        .capacity = DEVICE_DOMAIN_DMA_POOL_COUNT,
        .pool_bytes = DEVICE_DOMAIN_DMA_POOL_BYTES,
    };
    memset(irq_line_bindings, 0, sizeof(irq_line_bindings));
    platform_ops = *ops;
    device_count = 0U;
    platform_iommu_ready = iommu_ready;
    iommu_status = (device_domain_iommu_status_t){
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(iommu_status),
        .translation_enabled = iommu_ready ? 1U : 0U,
        .direct_assignment_ready = iommu_ready ? 1U : 0U,
    };
    operation_busy = 0U;
    pending_irq_lines = 0U;
    initialized = true;
    return true;
}

int device_domain_register(const device_domain_profile_t *profile,
                           uint32_t pci_location, uint32_t *device_out) {
    if (!initialized || !profile_valid(profile) || device_out == NULL ||
        pci_location == 0xFFFFFFFFU) return -22;
    if (!begin_operation()) return -16;
    if (device_count >= DEVICE_DOMAIN_MAX_DEVICES) {
        end_operation();
        return -28;
    }
    for (uint32_t index = 0U; index < device_count; ++index) {
        if (devices[index].pci_location == pci_location) {
            end_operation();
            return -17;
        }
    }
    if (!platform_ops.claim_device(pci_location, profile)) {
        end_operation();
        return -16;
    }
    bool irqless_io = profile_is_irqless_mediated_io(profile);
    bool intx_disabled = irqless_io || platform_ops.mask_irq(pci_location);
    bool irq_pic_fallback = false;
    bool irq_masked = intx_disabled;
    if (!irq_masked &&
        (profile->flags & DEVICE_DOMAIN_PROFILE_LEGACY_INTX_PIC) != 0U) {
        irq_pic_fallback = pic_mask_location(pci_location);
        irq_masked = irq_pic_fallback;
#ifndef REIST_HOST_TEST
        if (irq_pic_fallback)
            printf("DEVICE_DOMAIN: legacy INTx PIC fallback pci=%08X\n",
                   pci_location);
#endif
    }
    bool mastering_disabled = platform_ops.set_bus_master(pci_location, false);
#ifndef REIST_HOST_TEST
    if (!irq_masked || !mastering_disabled)
        printf("DEVICE_DOMAIN: initial fence pci=%08X intx=%u busmaster=%u\n",
               pci_location, intx_disabled ? 1U : 0U,
               mastering_disabled ? 1U : 0U);
#endif
    uint32_t index = device_count++;
    devices[index] = (device_slot_t){
        .registered = 1U,
        .pci_location = pci_location,
        .profile = *profile,
        .state = irq_masked && mastering_disabled
            ? DEVICE_DOMAIN_AVAILABLE : DEVICE_DOMAIN_UNSUPPORTED,
        .generation = 1U,
        .irq_pic_fallback = irq_pic_fallback ? 1U : 0U,
    };
    *device_out = index;
    end_operation();
    return irq_masked && mastering_disabled ? 0 : -5;
}

static bool region_rule_struct_valid(const device_domain_region_rule_t *rule) {
    if (rule == NULL || rule->region_index >= 6U || rule->reserved != 0U)
        return false;
    if (rule->kind == DEVICE_DOMAIN_REGION_RULE_VALUE) {
        if (rule->width != 1U && rule->width != 2U && rule->width != 4U)
            return false;
        uint32_t width_mask = rule->width == 1U ? 0xFFU :
            rule->width == 2U ? 0xFFFFU : UINT32_MAX;
        return (rule->offset & (rule->width - 1U)) == 0U &&
            (rule->writable_mask & ~width_mask) == 0U;
    }
    return (rule->kind == DEVICE_DOMAIN_REGION_RULE_DMA_ADDRESS ||
            rule->kind ==
                DEVICE_DOMAIN_REGION_RULE_DMA_DESCRIPTOR_ADDRESS) &&
        rule->width == 8U && (rule->offset & 7U) == 0U &&
        rule->writable_mask == 0U;
}

static bool region_policy_aperture(
        const device_domain_region_policy_t *policy, uint32_t region_index,
        uint32_t *aperture_out) {
    if (policy == NULL || aperture_out == NULL || region_index >= 6U)
        return false;
    uint32_t aperture = policy->readable_bytes[region_index];
    for (uint32_t index = 0U; index < policy->rule_count; ++index) {
        const device_domain_region_rule_t *rule = &policy->rules[index];
        if (rule->region_index != region_index) continue;
        if (rule->offset > UINT32_MAX - rule->width) return false;
        uint32_t end = rule->offset + rule->width;
        if (end > aperture) aperture = end;
    }
    if (aperture == 0U || aperture > DEVICE_DOMAIN_MAX_REGION_BYTES)
        return false;
    *aperture_out = aperture;
    return true;
}

int device_domain_install_region_policy(
        uint32_t device_index, const device_domain_region_policy_t *policy) {
    if (!initialized || policy == NULL || device_index >= device_count ||
        policy->version != DEVICE_DOMAIN_ABI_VERSION ||
        policy->struct_size != sizeof(*policy) ||
        policy->rule_count > DEVICE_DOMAIN_MAX_REGION_RULES ||
        policy->reserved != 0U) return -22;
    for (uint32_t rule = 0U; rule < policy->rule_count; ++rule)
        if (!region_rule_struct_valid(&policy->rules[rule])) return -22;
    for (uint32_t first = 0U; first < policy->rule_count; ++first) {
        const device_domain_region_rule_t *rule = &policy->rules[first];
        for (uint32_t second = first + 1U; second < policy->rule_count;
             ++second) {
            const device_domain_region_rule_t *other = &policy->rules[second];
            if (rule->region_index != other->region_index) continue;
            uint32_t rule_end = rule->offset + rule->width;
            uint32_t other_end = other->offset + other->width;
            if (rule_end < rule->offset || other_end < other->offset ||
                (rule->offset < other_end && other->offset < rule_end))
                return -22;
        }
    }
    for (uint32_t index = policy->rule_count;
         index < DEVICE_DOMAIN_MAX_REGION_RULES; ++index) {
        const device_domain_region_rule_t *rule = &policy->rules[index];
        if (rule->region_index != 0U || rule->offset != 0U ||
            rule->width != 0U || rule->kind != 0U ||
            rule->writable_mask != 0U || rule->reserved != 0U) return -22;
    }
    if (!begin_operation()) return -16;
    device_slot_t *device = &devices[device_index];
    if (device->registered == 0U || device->state != DEVICE_DOMAIN_AVAILABLE ||
        device->region_policy_installed != 0U) {
        end_operation();
        return -16;
    }
    device_domain_region_info_t regions[6] = {{0}};
    bool used[6] = {false};
    bool valid = true;
    for (uint32_t region = 0U; region < 6U; ++region)
        used[region] = policy->readable_bytes[region] != 0U;
    for (uint32_t rule = 0U; rule < policy->rule_count; ++rule)
        used[policy->rules[rule].region_index] = true;
    for (uint32_t region = 0U; region < 6U; ++region) {
        if (!used[region]) continue;
        uint32_t aperture = 0U;
        if (!platform_ops.describe_region(
                device->pci_location, region, &regions[region]) ||
            !region_policy_aperture(policy, region, &aperture)) {
            valid = false;
            break;
        }
        uint64_t physical_length =
            ((uint64_t)regions[region].length_high << 32U) |
            regions[region].length_low;
        if (physical_length == 0U || aperture > physical_length) {
            valid = false;
            break;
        }
        /* Platform preparation and every later mediated access receive only
         * the immutable policy aperture, never the full physical BAR. */
        regions[region].length_low = aperture;
        regions[region].length_high = 0U;
    }
    for (uint32_t rule = 0U; valid && rule < policy->rule_count; ++rule) {
        const device_domain_region_rule_t *entry = &policy->rules[rule];
        const device_domain_region_info_t *region =
            &regions[entry->region_index];
        if (entry->offset > region->length_low ||
            entry->width > region->length_low - entry->offset ||
            ((entry->kind == DEVICE_DOMAIN_REGION_RULE_DMA_ADDRESS ||
              entry->kind ==
                  DEVICE_DOMAIN_REGION_RULE_DMA_DESCRIPTOR_ADDRESS) &&
             (region->flags & DEVICE_DOMAIN_REGION_MMIO) == 0U)) valid = false;
    }
    for (uint32_t region = 0U; valid && region < 6U; ++region)
        if (used[region] && !platform_ops.prepare_region(&regions[region]))
            valid = false;
    if (!valid) {
        end_operation();
        return -95;
    }
    device->region_policy = *policy;
    device->region_policy_installed = 1U;
    end_operation();
    return 0;
}

static bool dma_relocation_rule_zero(
        const device_domain_dma_relocation_rule_t *rule) {
    return rule->destination_pool_offset == 0U &&
        rule->source_pool_offset == 0U && rule->shift_right == 0U &&
        rule->width == 0U && rule->fixed_bits == 0U;
}

static bool dma_relocation_rule_equal(
        const device_domain_dma_relocation_rule_t *left,
        const device_domain_dma_relocation_rule_t *right) {
    return left->destination_pool_offset == right->destination_pool_offset &&
        left->source_pool_offset == right->source_pool_offset &&
        left->shift_right == right->shift_right &&
        left->width == right->width && left->fixed_bits == right->fixed_bits;
}

static bool dma_relocation_rule_valid(
        const device_domain_dma_relocation_rule_t *rule,
        uint32_t capacity) {
    if (rule == NULL || rule->width != sizeof(uint64_t) ||
        rule->destination_pool_offset < DEVICE_DOMAIN_DMA_DATA_OFFSET ||
        rule->destination_pool_offset > capacity - rule->width ||
        (rule->destination_pool_offset & (sizeof(uint64_t) - 1U)) != 0U ||
        rule->source_pool_offset < DEVICE_DOMAIN_DMA_DATA_OFFSET ||
        rule->source_pool_offset > capacity - 4096U ||
        (rule->source_pool_offset & 4095U) != 0U ||
        rule->shift_right > 31U)
        return false;
    const uint64_t variable_address_bits =
        ((uint64_t)UINT32_MAX & ~4095ULL) >> rule->shift_right;
    return (rule->fixed_bits & variable_address_bits) == 0U;
}

int device_domain_install_dma_relocation_policy(
        uint32_t device_index,
        const device_domain_dma_relocation_policy_t *policy) {
    if (!initialized || policy == NULL || device_index >= device_count ||
        policy->version != DEVICE_DOMAIN_ABI_VERSION ||
        policy->struct_size != sizeof(*policy) || policy->policy_count == 0U ||
        policy->policy_count > DEVICE_DOMAIN_DMA_RELOCATION_MAX_POLICIES ||
        policy->reserved != 0U)
        return -22;
    const device_slot_t *registered = &devices[device_index];
    if ((registered->profile.flags & DEVICE_DOMAIN_PROFILE_MEDIATED_DMA) == 0U)
        return -95;
    const uint32_t capacity =
        (registered->profile.flags & DEVICE_DOMAIN_PROFILE_LARGE_DMA_POOL) != 0U
            ? DEVICE_DOMAIN_DMA_LARGE_POOL_BYTES
            : DEVICE_DOMAIN_DMA_POOL_BYTES;
    for (uint32_t template_index = 0U;
         template_index < policy->policy_count; ++template_index) {
        const device_domain_dma_relocation_template_t *relocation =
            &policy->policies[template_index];
        if (relocation->policy_id == 0U || relocation->rule_count == 0U ||
            relocation->rule_count > DEVICE_DOMAIN_DMA_RELOCATION_MAX_RULES ||
            relocation->reserved[0] != 0U ||
            relocation->reserved[1] != 0U)
            return -22;
        for (uint32_t prior = 0U; prior < template_index; ++prior)
            if (policy->policies[prior].policy_id == relocation->policy_id)
                return -22;
        for (uint32_t rule = 0U; rule < relocation->rule_count; ++rule) {
            if (!dma_relocation_rule_valid(&relocation->rules[rule], capacity))
                return -22;
            for (uint32_t prior = 0U; prior < rule; ++prior)
                if (relocation->rules[prior].destination_pool_offset ==
                    relocation->rules[rule].destination_pool_offset)
                    return -22;
        }
        for (uint32_t rule = relocation->rule_count;
             rule < DEVICE_DOMAIN_DMA_RELOCATION_MAX_RULES; ++rule)
            if (!dma_relocation_rule_zero(&relocation->rules[rule]))
                return -22;
    }
    for (uint32_t template_index = policy->policy_count;
         template_index < DEVICE_DOMAIN_DMA_RELOCATION_MAX_POLICIES;
         ++template_index) {
        const device_domain_dma_relocation_template_t *relocation =
            &policy->policies[template_index];
        if (relocation->policy_id != 0U || relocation->rule_count != 0U ||
            relocation->reserved[0] != 0U ||
            relocation->reserved[1] != 0U)
            return -22;
        for (uint32_t rule = 0U;
             rule < DEVICE_DOMAIN_DMA_RELOCATION_MAX_RULES; ++rule)
            if (!dma_relocation_rule_zero(&relocation->rules[rule]))
                return -22;
    }
    if (!begin_operation()) return -16;
    device_slot_t *device = &devices[device_index];
    if (device->registered == 0U || device->state != DEVICE_DOMAIN_AVAILABLE ||
        device->dma_relocation_policy_installed != 0U) {
        end_operation();
        return -16;
    }
    device->dma_relocation_policy = *policy;
    device->dma_relocation_policy_installed = 1U;
    end_operation();
    return 0;
}

static bool dma_vm_page_mode_template_zero(
        const device_domain_dma_vm_page_mode_template_t *entry) {
    return entry->policy_id == 0U && entry->region_index == 0U &&
        entry->register_offset == 0U && entry->width == 0U &&
        entry->writable_mask == 0U && entry->value == 0U;
}

int device_domain_install_dma_vm_page_mode_policy(
        uint32_t device_index,
        const device_domain_dma_vm_page_mode_policy_t *policy) {
    if (!initialized || policy == NULL || device_index >= device_count ||
        policy->version != DEVICE_DOMAIN_ABI_VERSION ||
        policy->struct_size != sizeof(*policy) || policy->policy_count == 0U ||
        policy->policy_count > DEVICE_DOMAIN_DMA_VM_PAGE_MODE_MAX_POLICIES ||
        policy->reserved != 0U)
        return -22;
    const device_slot_t *registered = &devices[device_index];
    if ((registered->profile.flags & DEVICE_DOMAIN_PROFILE_MEDIATED_DMA) == 0U ||
        registered->region_policy_installed == 0U)
        return -95;
    for (uint32_t index = 0U; index < policy->policy_count; ++index) {
        const device_domain_dma_vm_page_mode_template_t *entry =
            &policy->policies[index];
        if (entry->policy_id == 0U || entry->region_index >= 6U ||
            entry->width != sizeof(uint32_t) ||
            (entry->register_offset & (sizeof(uint32_t) - 1U)) != 0U ||
            entry->writable_mask == 0U ||
            (entry->value & ~entry->writable_mask) != 0U ||
            entry->register_offset >
                registered->region_policy.readable_bytes[entry->region_index] ||
            entry->width > registered->region_policy
                    .readable_bytes[entry->region_index] -
                entry->register_offset)
            return -22;
        for (uint32_t prior = 0U; prior < index; ++prior)
            if (policy->policies[prior].policy_id == entry->policy_id)
                return -22;
    }
    for (uint32_t index = policy->policy_count;
         index < DEVICE_DOMAIN_DMA_VM_PAGE_MODE_MAX_POLICIES; ++index)
        if (!dma_vm_page_mode_template_zero(&policy->policies[index]))
            return -22;
    if (!begin_operation()) return -16;
    device_slot_t *device = &devices[device_index];
    if (device->registered == 0U || device->state != DEVICE_DOMAIN_AVAILABLE ||
        device->dma_vm_page_mode_policy_installed != 0U) {
        end_operation();
        return -16;
    }
    device->dma_vm_page_mode_policy = *policy;
    device->dma_vm_page_mode_policy_installed = 1U;
    end_operation();
    return 0;
}

static bool gr_firmware_image_valid(
        const device_domain_gr_firmware_image_t *image,
        uint32_t readable_bytes) {
    if (image == NULL || image->word_count == 0U ||
        image->word_count > DEVICE_DOMAIN_GR_FIRMWARE_MAX_WORDS ||
        image->crc32 == 0U || (image->pool_offset & 3U) != 0U ||
        (image->falcon_base & 0xFFFU) != 0U ||
        (image->memory_kind != DEVICE_DOMAIN_GR_FIRMWARE_DMEM &&
         image->memory_kind != DEVICE_DOMAIN_GR_FIRMWARE_IMEM) ||
        image->reserved[0] != 0U || image->reserved[1] != 0U ||
        image->reserved[2] != 0U)
        return false;
    const uint32_t bytes = image->word_count * sizeof(uint32_t);
    return image->pool_offset >= DEVICE_DOMAIN_DMA_DESCRIPTOR_BYTES &&
        image->pool_offset <= DEVICE_DOMAIN_DMA_LARGE_POOL_BYTES &&
        bytes <= DEVICE_DOMAIN_DMA_LARGE_POOL_BYTES - image->pool_offset &&
        image->falcon_base <= readable_bytes &&
        0x1C8U <= readable_bytes - image->falcon_base;
}

int device_domain_install_gr_firmware_policy(
        uint32_t device_index,
        const device_domain_gr_firmware_policy_t *policy) {
    if (!initialized || policy == NULL || device_index >= device_count ||
        policy->version != DEVICE_DOMAIN_ABI_VERSION ||
        policy->struct_size != sizeof(*policy) || policy->policy_id == 0U ||
        policy->region_index >= 6U || policy->image_count !=
            DEVICE_DOMAIN_GR_FIRMWARE_IMAGE_COUNT ||
        (policy->pmc_enable_offset & 3U) != 0U ||
        policy->pmc_gr_mask == 0U ||
        (policy->pmc_gr_mask & (policy->pmc_gr_mask - 1U)) != 0U ||
        policy->reserved != 0U)
        return -22;
    const device_slot_t *registered = &devices[device_index];
    if ((registered->profile.flags & DEVICE_DOMAIN_PROFILE_MEDIATED_DMA) == 0U ||
        registered->region_policy_installed == 0U)
        return -95;
    const uint32_t readable = registered->region_policy
        .readable_bytes[policy->region_index];
    if (policy->pmc_enable_offset > readable ||
        sizeof(uint32_t) > readable - policy->pmc_enable_offset)
        return -22;
    for (uint32_t index = 0U;
         index < DEVICE_DOMAIN_GR_FIRMWARE_IMAGE_COUNT; ++index) {
        const device_domain_gr_firmware_image_t *image =
            &policy->images[index];
        const uint32_t expected_kind = (index & 1U) == 0U
            ? DEVICE_DOMAIN_GR_FIRMWARE_DMEM
            : DEVICE_DOMAIN_GR_FIRMWARE_IMEM;
        if (!gr_firmware_image_valid(image, readable) ||
            image->memory_kind != expected_kind ||
            (image->memory_kind == DEVICE_DOMAIN_GR_FIRMWARE_IMEM &&
             (image->word_count & 0x3FU) != 0U) ||
            ((index & 1U) != 0U && image->falcon_base !=
                policy->images[index - 1U].falcon_base) ||
            (index >= 2U &&
             image->falcon_base == policy->images[0].falcon_base))
            return -22;
        const uint32_t image_end = image->pool_offset +
            image->word_count * sizeof(uint32_t);
        for (uint32_t prior = 0U; prior < index; ++prior) {
            const device_domain_gr_firmware_image_t *other =
                &policy->images[prior];
            const uint32_t other_end = other->pool_offset +
                other->word_count * sizeof(uint32_t);
            if (image->pool_offset < other_end &&
                other->pool_offset < image_end)
                return -22;
        }
    }
    if (!begin_operation()) return -16;
    device_slot_t *device = &devices[device_index];
    if (device->registered == 0U || device->state != DEVICE_DOMAIN_AVAILABLE ||
        device->gr_firmware_policy_installed != 0U) {
        end_operation();
        return -16;
    }
    device->gr_firmware_policy = *policy;
    device->gr_firmware_policy_installed = 1U;
    end_operation();
    return 0;
}

int device_domain_install_gr_prerequisite_policy(
        uint32_t device_index,
        const device_domain_gr_prerequisite_policy_t *policy) {
    if (!initialized || policy == NULL || device_index >= device_count ||
        policy->version != DEVICE_DOMAIN_ABI_VERSION ||
        policy->struct_size != sizeof(*policy) || policy->policy_id == 0U ||
        policy->region_index >= 6U || policy->vram_region_index >= 6U ||
        policy->region_index == policy->vram_region_index ||
        policy->execution_pool_offset < DEVICE_DOMAIN_DMA_DESCRIPTOR_BYTES ||
        policy->execution_max_operations !=
            DEVICE_DOMAIN_GR_EXECUTION_OP_CAPACITY ||
        policy->execution_flags != GK208_GR_EXECUTION_FLAGS ||
        policy->scanout_bytes == 0U || policy->vram_aperture_bytes == 0U ||
        policy->scanout_offset > policy->vram_aperture_bytes ||
        policy->scanout_bytes >
            policy->vram_aperture_bytes - policy->scanout_offset ||
        policy->fault_buffer_bytes != (1U << policy->fb_page_shift) ||
        policy->fault_buffer_alignment != policy->fault_buffer_bytes ||
        policy->fb_page_shift != 17U ||
        policy->fbp_max == 0U ||
        policy->fbp_max > DEVICE_DOMAIN_GR_MAX_FBPS ||
        policy->fbpa_max < policy->fbp_max ||
        policy->fbpa_max > DEVICE_DOMAIN_GR_MAX_FBPAS ||
        policy->reserved[0] != 0U || policy->reserved[1] != 0U)
        return -22;
    const uint32_t image_bytes = DEVICE_DOMAIN_GR_EXECUTION_HEADER_BYTES +
        policy->execution_max_operations * DEVICE_DOMAIN_GR_EXECUTION_OP_BYTES;
    if (policy->execution_pool_offset > DEVICE_DOMAIN_DMA_LARGE_POOL_BYTES ||
        image_bytes >
            DEVICE_DOMAIN_DMA_LARGE_POOL_BYTES - policy->execution_pool_offset)
        return -22;
    const device_slot_t *registered = &devices[device_index];
    if ((registered->profile.flags &
            (DEVICE_DOMAIN_PROFILE_MEDIATED_DMA |
             DEVICE_DOMAIN_PROFILE_LARGE_DMA_POOL)) !=
            (DEVICE_DOMAIN_PROFILE_MEDIATED_DMA |
             DEVICE_DOMAIN_PROFILE_LARGE_DMA_POOL) ||
        registered->region_policy_installed == 0U ||
        registered->region_policy.readable_bytes[policy->region_index] <
            GK208_GR_GPC_UNIT_BASE +
                (DEVICE_DOMAIN_GR_MAX_GPCS - 1U) *
                    GK208_GR_GPC_UNIT_STRIDE +
                GK208_GR_GPC_TPC_COUNT_OFFSET + sizeof(uint32_t))
        return -95;
    device_domain_region_info_t vram;
    if (!platform_ops.describe_region(registered->pci_location,
            policy->vram_region_index, &vram) ||
        (vram.flags & DEVICE_DOMAIN_REGION_MMIO) == 0U ||
        (vram.flags & DEVICE_DOMAIN_REGION_PIO) != 0U ||
        vram.length_high != 0U ||
        vram.length_low != policy->vram_aperture_bytes)
        return -95;
    if (!begin_operation()) return -16;
    device_slot_t *device = &devices[device_index];
    if (device->registered == 0U || device->state != DEVICE_DOMAIN_AVAILABLE ||
        device->gr_prerequisite_policy_installed != 0U) {
        end_operation();
        return -16;
    }
    device->gr_prerequisite_policy = *policy;
    device->gr_prerequisite_policy_installed = 1U;
    end_operation();
    return 0;
}

int device_domain_install_gr_channel_policy(
        uint32_t device_index,
        const device_domain_gr_channel_policy_t *policy) {
    if (!initialized || policy == NULL || device_index >= device_count ||
        policy->version != DEVICE_DOMAIN_ABI_VERSION ||
        policy->struct_size != sizeof(*policy) || policy->policy_id == 0U ||
        policy->width == 0U || policy->height == 0U ||
        policy->width > 4096U || policy->height > 4096U ||
        policy->width > UINT32_MAX / 4U ||
        policy->pitch < policy->width * 4U || policy->pitch > 65536U ||
        (policy->pitch & 3U) != 0U ||
        policy->height > UINT32_MAX / policy->pitch ||
        policy->channel_id != GK208_CHANNEL_ID ||
        policy->channel_id >= GK208_CHANNEL_LIMIT ||
        policy->reserved[0] != 0U || policy->reserved[1] != 0U ||
        policy->reserved[2] != 0U) return -22;
    const device_slot_t *registered = &devices[device_index];
    if (registered->gr_prerequisite_policy_installed == 0U ||
        registered->gr_prerequisite_policy.policy_id != policy->policy_id ||
        registered->gr_prerequisite_policy.scanout_bytes !=
            policy->pitch * policy->height)
        return -95;
    if (!begin_operation()) return -16;
    device_slot_t *device = &devices[device_index];
    if (device->registered == 0U || device->state != DEVICE_DOMAIN_AVAILABLE ||
        device->gr_channel_policy_installed != 0U) {
        end_operation();
        return -16;
    }
    device->gr_channel_policy = *policy;
    device->gr_channel_policy_installed = 1U;
    end_operation();
    return 0;
}

int device_domain_claim(int pid, uint32_t process_generation, uint32_t device,
                        uint32_t mode, device_domain_handle_t *handle_out) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        handle_out == NULL || device >= device_count) return -22;
    if (!begin_operation()) return -16;
    device_slot_t *slot = &devices[device];
    group_owner_t *group = &groups[slot->profile.isolation_group];
    if (slot->registered == 0U || slot->state != DEVICE_DOMAIN_AVAILABLE) {
        end_operation();
        return -16;
    }
    if (!mode_allowed(slot, mode)) {
        end_operation();
        return -95;
    }
    if (group->claim_count != 0U &&
        (group->owner_pid != pid ||
         group->owner_generation != process_generation)) {
        end_operation();
        return -16;
    }
    if (slot->generation == DEVICE_DOMAIN_HANDLE_GENERATION_MAX ||
        group->claim_count == UINT32_MAX) {
        slot->state = DEVICE_DOMAIN_UNSUPPORTED;
        end_operation();
        return -75;
    }
    group->owner_pid = pid;
    group->owner_generation = process_generation;
    ++group->claim_count;
    slot->owner_pid = pid;
    slot->owner_generation = process_generation;
    slot->mode = mode;
    slot->mediated_io_quiesced = 0U;
    slot->state = DEVICE_DOMAIN_CLAIMED;
    *handle_out = make_handle(device, slot->generation);
    end_operation();
    return 0;
}

int device_domain_open_region(int pid, uint32_t process_generation,
                              const device_domain_region_request_t *request,
                              device_domain_region_info_t *region_out) {
    const uint32_t known_rights = DEVICE_DOMAIN_REGION_DESCRIBE |
        DEVICE_DOMAIN_REGION_MAP_READ | DEVICE_DOMAIN_REGION_MAP_WRITE |
        DEVICE_DOMAIN_REGION_ACCESS_READ |
        DEVICE_DOMAIN_REGION_ACCESS_WRITE;
    if (request == NULL || region_out == NULL ||
        request->version != DEVICE_DOMAIN_ABI_VERSION ||
        request->struct_size != sizeof(*request) ||
        request->device == DEVICE_DOMAIN_INVALID_HANDLE ||
        request->region_index >= 6U || request->rights == 0U ||
        (request->rights & ~known_rights) != 0U || request->flags != 0U ||
        request->reserved[0] != 0U || request->reserved[1] != 0U) return -22;
    if (!begin_operation()) return -16;
    device_slot_t *device = owned_slot(
        pid, process_generation, request->device);
    bool map_requested = (request->rights &
        (DEVICE_DOMAIN_REGION_MAP_READ | DEVICE_DOMAIN_REGION_MAP_WRITE)) != 0U;
    if (device == NULL || device->state == DEVICE_DOMAIN_FENCED ||
        device->state == DEVICE_DOMAIN_UNSUPPORTED || map_requested) {
        end_operation();
        return device == NULL ? -9 : -95;
    }
    bool read_requested = (request->rights &
        DEVICE_DOMAIN_REGION_ACCESS_READ) != 0U;
    bool write_requested = (request->rights &
        DEVICE_DOMAIN_REGION_ACCESS_WRITE) != 0U;
    bool writable = false;
    if (device->region_policy_installed != 0U) {
        for (uint32_t rule = 0U;
             rule < device->region_policy.rule_count; ++rule)
            if (device->region_policy.rules[rule].region_index ==
                    request->region_index) writable = true;
    }
    if ((read_requested || write_requested) &&
        (device->region_policy_installed == 0U ||
         (read_requested && device->region_policy.readable_bytes[
             request->region_index] == 0U) ||
         (write_requested && !writable))) {
        end_operation();
        return -13;
    }
    if (resource_exists((uint32_t)(device - devices), device->generation,
            DEVICE_DOMAIN_RESOURCE_REGION, request->region_index)) {
        end_operation();
        return -17;
    }
    int resource_index = available_resource_slot();
    if (resource_index < 0) {
        end_operation();
        return -28;
    }
    device_domain_region_info_t region = {0};
    if (!platform_ops.describe_region(device->pci_location,
            request->region_index, &region)) {
        end_operation();
        return -95;
    }
    const uint32_t known_flags = DEVICE_DOMAIN_REGION_MMIO |
        DEVICE_DOMAIN_REGION_PIO | DEVICE_DOMAIN_REGION_64BIT |
        DEVICE_DOMAIN_REGION_PREFETCHABLE;
    uint64_t length = ((uint64_t)region.length_high << 32U) |
        region.length_low;
    bool one_kind = ((region.flags & DEVICE_DOMAIN_REGION_MMIO) != 0U) !=
        ((region.flags & DEVICE_DOMAIN_REGION_PIO) != 0U);
    if (region.version != DEVICE_DOMAIN_ABI_VERSION ||
        region.struct_size != sizeof(region) ||
        region.region_index != request->region_index ||
        (region.flags & ~known_flags) != 0U || !one_kind || length == 0U ||
        region.resource != DEVICE_DOMAIN_INVALID_HANDLE ||
        region.reserved[0] != 0U || region.reserved[1] != 0U) {
        end_operation();
        return -84;
    }
    uint32_t aperture = 0U;
    bool has_aperture = device->region_policy_installed != 0U &&
        region_policy_aperture(&device->region_policy,
                               request->region_index, &aperture);
    if ((read_requested || write_requested) && !has_aperture) {
        end_operation();
        return -13;
    }
    if (has_aperture) {
        if ((uint64_t)aperture > length) {
            end_operation();
            return -84;
        }
        region.length_low = aperture;
        region.length_high = 0U;
    }
    if ((read_requested || write_requested) &&
        !platform_ops.prepare_region(&region)) {
        end_operation();
        return -95;
    }
    region.rights = request->rights;
    region.resource = publish_resource((uint32_t)resource_index,
        DEVICE_DOMAIN_RESOURCE_REGION, (uint32_t)(device - devices), device,
        request->region_index);
    resources[resource_index].region = region;
    *region_out = region;
    end_operation();
    return 0;
}

int device_domain_bind_irq(int pid, uint32_t process_generation,
                           const device_domain_irq_request_t *request,
                           device_domain_resource_handle_t *resource_out) {
    if (request == NULL || resource_out == NULL ||
        request->version != DEVICE_DOMAIN_ABI_VERSION ||
        request->struct_size != sizeof(*request) ||
        request->device == DEVICE_DOMAIN_INVALID_HANDLE ||
        request->endpoint_capability == 0U || request->flags != 0U ||
        request->reserved[0] != 0U || request->reserved[1] != 0U ||
        request->reserved[2] != 0U) return -22;
    if (!begin_operation()) return -16;
    device_slot_t *device = owned_slot(
        pid, process_generation, request->device);
    if (device == NULL || device->state != DEVICE_DOMAIN_CLAIMED ||
        device->irq_bound != 0U) {
        end_operation();
        return device == NULL ? -9 : -22;
    }
    int resource_index = available_resource_slot();
    if (resource_index < 0) {
        end_operation();
        return -28;
    }
    if (!platform_ops.bind_irq(device->pci_location, pid,
            process_generation, request->endpoint_capability)) {
        end_operation();
        return -13;
    }
    device->irq_bound = 1U;
    device->irq_capability = request->endpoint_capability;
    *resource_out = publish_resource((uint32_t)resource_index,
        DEVICE_DOMAIN_RESOURCE_IRQ, (uint32_t)(device - devices), device,
        request->endpoint_capability);
    device->irq_resource = *resource_out;
    end_operation();
    return 0;
}

int device_domain_bind_dma(int pid, uint32_t process_generation,
                           const device_domain_dma_request_t *request,
                           device_domain_resource_handle_t *resource_out) {
    if (request == NULL || resource_out == NULL ||
        request->version != DEVICE_DOMAIN_ABI_VERSION ||
        request->struct_size != sizeof(*request) ||
        request->device == DEVICE_DOMAIN_INVALID_HANDLE ||
        request->flags == 0U ||
        (request->flags & ~(DEVICE_DOMAIN_DMA_TO_DEVICE |
                            DEVICE_DOMAIN_DMA_FROM_DEVICE)) != 0U ||
        request->reserved[0] != 0U || request->reserved[1] != 0U ||
        request->reserved[2] != 0U) return -22;
    if (!begin_operation()) return -16;
    device_slot_t *device = owned_slot(
        pid, process_generation, request->device);
    if (device == NULL || device->state != DEVICE_DOMAIN_CLAIMED ||
        device->dma_bound != 0U || !mode_allowed(device, device->mode)) {
        end_operation();
        return device == NULL ? -9 : -22;
    }
    if (device->mode == DEVICE_DOMAIN_MODE_MEDIATED &&
        (device->profile.flags & DEVICE_DOMAIN_PROFILE_MEDIATED_DMA) == 0U) {
        end_operation();
        return -95;
    }
    int resource_index = available_resource_slot();
    if (resource_index < 0) {
        end_operation();
        return -28;
    }
    uint32_t platform_capability = request->dma_capability;
    int pool_index = -1;
    if (device->mode == DEVICE_DOMAIN_MODE_MEDIATED) {
        if (request->dma_capability != 0U ||
            (pool_index = available_dma_pool()) < 0) {
            if (request->dma_capability == 0U)
                increment_saturating(&dma_pool_stats.capacity_rejections);
            end_operation();
            return request->dma_capability != 0U ? -22 : -28;
        }
        platform_capability = (uint32_t)pool_index + 1U;
    } else if (request->dma_capability == 0U) {
        end_operation();
        return -22;
    }
    if (!platform_ops.bind_dma(device->pci_location, pid, process_generation,
                               device->mode, platform_capability)) {
        end_operation();
        return -13;
    }
    if (pool_index >= 0) {
        uint32_t capacity =
            (device->profile.flags & DEVICE_DOMAIN_PROFILE_LARGE_DMA_POOL) != 0U
                ? DEVICE_DOMAIN_DMA_LARGE_POOL_BYTES
                : DEVICE_DOMAIN_DMA_POOL_BYTES;
        memset(dma_pool_storage[pool_index], 0, capacity);
        dma_pools[pool_index] = (dma_pool_slot_t){
            .active = 1U,
            .device_slot = (uint32_t)(device - devices),
            .device_generation = device->generation,
            .owner_pid = pid,
            .owner_generation = process_generation,
            .direction = request->flags,
            .capacity = capacity,
        };
        ++dma_pool_stats.active_pools;
        if (dma_pool_stats.active_pools > dma_pool_stats.peak_active_pools)
            dma_pool_stats.peak_active_pools = dma_pool_stats.active_pools;
    }
    device->dma_bound = 1U;
    device->dma_capability = platform_capability;
    device->state = DEVICE_DOMAIN_DMA_BOUND;
    *resource_out = publish_resource((uint32_t)resource_index,
        DEVICE_DOMAIN_RESOURCE_DMA, (uint32_t)(device - devices), device,
        platform_capability);
    device->dma_resource = *resource_out;
    end_operation();
    return 0;
}

int device_domain_activate(int pid, uint32_t process_generation,
                           device_domain_handle_t handle) {
    if (!begin_operation()) return -16;
    device_slot_t *device = owned_slot(pid, process_generation, handle);
    if (device == NULL || device->state != DEVICE_DOMAIN_DMA_BOUND ||
        device->irq_bound == 0U || device->dma_bound == 0U) {
        end_operation();
        return device == NULL ? -9 : -22;
    }
    if (!platform_ops.set_bus_master(device->pci_location, true) ||
        !unmask_device_irq(device)) {
        (void)fence_slot(device);
        end_operation();
        return -5;
    }
    device->irq_window_start_ms = 0U;
    device->irq_window_count = 0U;
    device->state = DEVICE_DOMAIN_ACTIVE;
    end_operation();
    return 0;
}

int device_domain_deactivate(int pid, uint32_t process_generation,
                             device_domain_handle_t handle) {
    if (!begin_operation()) return -16;
    device_slot_t *device = owned_slot(pid, process_generation, handle);
    if (device == NULL) {
        end_operation();
        return -9;
    }
    if (device->state == DEVICE_DOMAIN_DMA_BOUND) {
        end_operation();
        return 0;
    }
    if (device->state != DEVICE_DOMAIN_ACTIVE || device->irq_bound == 0U ||
        device->dma_bound == 0U) {
        end_operation();
        return -22;
    }
    bool irq_masked = mask_device_irq(device);
    bool mastering_disabled =
        platform_ops.set_bus_master(device->pci_location, false);
    if (!irq_masked || !mastering_disabled) {
        (void)fence_slot(device);
        end_operation();
        return -5;
    }
    device->irq_pending_count = 0U;
    device->irq_notification_sent = 0U;
    device->irq_deadline_ms = 0U;
    device->irq_window_start_ms = 0U;
    device->irq_window_count = 0U;
    device->state = DEVICE_DOMAIN_DMA_BOUND;
    end_operation();
    return 0;
}

int device_domain_fence(int pid, uint32_t process_generation,
                        device_domain_handle_t handle) {
    if (!begin_operation()) return -16;
    device_slot_t *device = owned_slot(pid, process_generation, handle);
    if (device == NULL) {
        end_operation();
        return -9;
    }
    if (device->state == DEVICE_DOMAIN_FENCED &&
        device->dma_vm_page_mode_active == 0U) {
        end_operation();
        return 0;
    }
    bool fenced = fence_slot(device);
    end_operation();
    return fenced ? 0 : -5;
}

int device_domain_mark_mediated_io_quiesced(
        int pid, uint32_t process_generation, device_domain_handle_t handle) {
    if (!begin_operation()) return -16;
    device_slot_t *device = owned_slot(pid, process_generation, handle);
    if (device == NULL ||
        !device_is_passive_mediated_io(device)) {
        end_operation();
        return device == NULL ? -9 : -95;
    }
    bool mastering_disabled =
        platform_ops.set_bus_master(device->pci_location, false);
    if (mastering_disabled) device->mediated_io_quiesced = 1U;
    end_operation();
    return mastering_disabled ? 0 : -5;
}

int device_domain_release(int pid, uint32_t process_generation,
                          device_domain_handle_t handle,
                          uint64_t deadline_ms) {
    if (!initialized || deadline_ms == 0U ||
        platform_ops.monotonic_ms() >= deadline_ms) return -110;
    if (!begin_operation()) return -16;
    device_slot_t *device = owned_slot(pid, process_generation, handle);
    if (device == NULL) {
        end_operation();
        return -9;
    }
    bool fenced = device->state == DEVICE_DOMAIN_FENCED &&
            device->dma_vm_page_mode_active == 0U
        ? true : fence_slot(device);
    bool mediated_reset = profile_is_irqless_mediated_io(&device->profile) &&
        device->mediated_io_quiesced != 0U;
    bool reset = fenced && platform_ops.monotonic_ms() < deadline_ms &&
        (mediated_reset ||
         platform_ops.reset(device->pci_location, deadline_ms)) &&
        platform_ops.monotonic_ms() < deadline_ms;
    if (!reset) {
        device->state = DEVICE_DOMAIN_FENCED;
        end_operation();
        return -5;
    }
    group_owner_t *group = &groups[device->profile.isolation_group];
    if (group->claim_count == 0U || group->owner_pid != pid ||
        group->owner_generation != process_generation) {
        device->state = DEVICE_DOMAIN_UNSUPPORTED;
        end_operation();
        return -84;
    }
    --group->claim_count;
    if (group->claim_count == 0U) *group = (group_owner_t){0};
    device->owner_pid = 0;
    device->owner_generation = 0U;
    device->mode = 0U;
    device->mediated_io_quiesced = 0U;
    if (device->generation == DEVICE_DOMAIN_HANDLE_GENERATION_MAX) {
        device->state = DEVICE_DOMAIN_UNSUPPORTED;
        end_operation();
        return -75;
    }
    ++device->generation;
    device->state = DEVICE_DOMAIN_AVAILABLE;
    end_operation();
    return 0;
}

void device_domain_process_cleanup(int pid, uint32_t process_generation) {
    if (!initialized || pid <= 0 || process_generation == 0U) return;
    for (uint32_t index = 0U; index < device_count; ++index) {
        device_slot_t *device = &devices[index];
        bool owned = device->owner_pid == pid &&
            device->owner_generation == process_generation;
        device_domain_handle_t handle =
            make_handle(index, device->generation);
        if (owned)
            (void)device_domain_fence(pid, process_generation, handle);
    }
}

int device_domain_recover_owner(int pid, uint32_t process_generation,
                                uint64_t deadline_ms) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        deadline_ms == 0U)
        return -22;
    if (platform_ops.monotonic_ms() >= deadline_ms) return -110;
    if (!begin_operation()) return -16;

    uint32_t owned_per_group[DEVICE_DOMAIN_MAX_GROUPS] = {0};
    uint32_t owned_count = 0U;
    bool metadata_valid = true;
    for (uint32_t index = 0U; index < device_count; ++index) {
        device_slot_t *device = &devices[index];
        if (device->owner_pid != pid ||
            device->owner_generation != process_generation) continue;
        ++owned_count;
        if (device->registered == 0U ||
            device->generation == DEVICE_DOMAIN_HANDLE_GENERATION_MAX ||
            device->profile.isolation_group == 0U ||
            device->profile.isolation_group >= DEVICE_DOMAIN_MAX_GROUPS) {
            metadata_valid = false;
            continue;
        }
        ++owned_per_group[device->profile.isolation_group];
    }
    if (owned_count == 0U) {
        end_operation();
        return -2;
    }
    for (uint32_t group_index = 1U;
         group_index < DEVICE_DOMAIN_MAX_GROUPS; ++group_index) {
        if (owned_per_group[group_index] == 0U) continue;
        const group_owner_t *group = &groups[group_index];
        if (group->owner_pid != pid ||
            group->owner_generation != process_generation ||
            group->claim_count != owned_per_group[group_index])
            metadata_valid = false;
    }

    bool fenced = metadata_valid;
    for (uint32_t index = 0U; index < device_count; ++index) {
        device_slot_t *device = &devices[index];
        if (device->owner_pid == pid &&
            device->owner_generation == process_generation &&
            device->state != DEVICE_DOMAIN_FENCED && !fence_slot(device))
            fenced = false;
    }
    if (!fenced || platform_ops.monotonic_ms() >= deadline_ms) {
        end_operation();
        return platform_ops.monotonic_ms() >= deadline_ms ? -110 : -5;
    }

    bool reset = true;
    for (uint32_t index = 0U; index < device_count; ++index) {
        device_slot_t *device = &devices[index];
        if (device->owner_pid != pid ||
            device->owner_generation != process_generation) continue;
        bool mediated_reset =
            profile_is_irqless_mediated_io(&device->profile) &&
            device->mediated_io_quiesced != 0U;
        if (platform_ops.monotonic_ms() >= deadline_ms ||
            (!mediated_reset &&
             !platform_ops.reset(device->pci_location, deadline_ms))) {
            reset = false;
        }
    }
    if (!reset || platform_ops.monotonic_ms() >= deadline_ms) {
        end_operation();
        return platform_ops.monotonic_ms() >= deadline_ms ? -110 : -5;
    }

    for (uint32_t group_index = 1U;
         group_index < DEVICE_DOMAIN_MAX_GROUPS; ++group_index) {
        if (owned_per_group[group_index] != 0U)
            groups[group_index] = (group_owner_t){0};
    }
    for (uint32_t index = 0U; index < device_count; ++index) {
        device_slot_t *device = &devices[index];
        if (device->owner_pid != pid ||
            device->owner_generation != process_generation) continue;
        device->owner_pid = 0;
        device->owner_generation = 0U;
        device->mode = 0U;
        device->mediated_io_quiesced = 0U;
        ++device->generation;
        device->state = DEVICE_DOMAIN_AVAILABLE;
    }
    end_operation();
    return 0;
}

int device_domain_status(uint32_t device, device_domain_status_t *status) {
    if (!initialized || status == NULL || device >= device_count) return -22;
    if (!begin_operation()) return -16;
    const device_slot_t *slot = &devices[device];
    memset(status, 0, sizeof(*status));
    status->version = DEVICE_DOMAIN_ABI_VERSION;
    status->struct_size = sizeof(*status);
    status->pci_location = slot->pci_location;
    status->isolation_group = slot->profile.isolation_group;
    status->state = slot->state;
    status->generation = slot->generation;
    status->owner_pid = slot->owner_pid;
    status->owner_generation = slot->owner_generation;
    status->mode = slot->mode;
    status->irq_bound = slot->irq_bound;
    status->dma_bound = slot->dma_bound;
    end_operation();
    return 0;
}

int device_domain_iommu_status(device_domain_iommu_status_t *status) {
    if (!initialized || status == NULL) return -22;
    if (!begin_operation()) return -16;
    *status = iommu_status;
    end_operation();
    return 0;
}

void device_domain_poll(uint64_t now_ms) {
    if (!initialized || !begin_operation()) return;
    uint32_t lines = __sync_lock_test_and_set(&pending_irq_lines, 0U);
    for (uint32_t irq = 0U; irq < PCI_LEGACY_IRQ_COUNT; ++irq) {
        uint32_t bit = 1U << irq;
        if ((lines & bit) == 0U) continue;
        bool matched = false;
        bool hold_pic_line = false;
        for (uint32_t index = 0U; index < device_count; ++index) {
            device_slot_t *device = &devices[index];
            const pci_device_t *pci = pci_find_location(device->pci_location);
            if (device->state != DEVICE_DOMAIN_ACTIVE ||
                device->irq_bound == 0U || pci == NULL ||
                pci->irq_line != irq) continue;
            matched = true;
            if (device->irq_pic_fallback != 0U) hold_pic_line = true;
            if (!irq_window_admit(device, now_ms) ||
                !mask_device_irq(device) ||
                device->irq_resource == DEVICE_DOMAIN_INVALID_HANDLE ||
                device->irq_sequence == DEVICE_DOMAIN_HANDLE_GENERATION_MAX) {
                (void)fence_slot(device);
                continue;
            }
            if (device->irq_pending_count == 0U) {
                ++device->irq_sequence;
                device->irq_pending_count = 1U;
                device->irq_notification_sent = 0U;
                device->irq_deadline_ms = UINT64_MAX - now_ms <
                        DEVICE_DOMAIN_IRQ_TIMEOUT_MS
                    ? UINT64_MAX : now_ms + DEVICE_DOMAIN_IRQ_TIMEOUT_MS;
            } else if (device->irq_pending_count != UINT32_MAX) {
                ++device->irq_pending_count;
            }
        }
        if ((pending_irq_lines & bit) == 0U &&
            !hold_pic_line &&
            !irq_pic_unmask_line((uint8_t)irq)) {
            (void)__sync_fetch_and_or(&pending_irq_lines, bit);
            for (uint32_t index = 0U; index < device_count; ++index) {
                device_slot_t *device = &devices[index];
                const pci_device_t *pci =
                    pci_find_location(device->pci_location);
                if (matched && device->state == DEVICE_DOMAIN_ACTIVE &&
                    pci != NULL && pci->irq_line == irq)
                    (void)fence_slot(device);
            }
        }
    }

    for (uint32_t index = 0U; index < device_count; ++index) {
        device_slot_t *device = &devices[index];
        if (device->state != DEVICE_DOMAIN_ACTIVE ||
            device->irq_pending_count == 0U) continue;
        if (now_ms >= device->irq_deadline_ms) {
            (void)fence_slot(device);
            continue;
        }
        if (device->irq_notification_sent != 0U) continue;
        device_domain_irq_message_t notification = {
            .version = DEVICE_DOMAIN_ABI_VERSION,
            .struct_size = sizeof(notification),
            .resource = device->irq_resource,
            .sequence = device->irq_sequence,
            .pending_count = device->irq_pending_count,
        };
        ipc_message_t message = {
            .version = IPC_MESSAGE_VERSION,
            .struct_size = sizeof(message),
            .length = sizeof(notification),
        };
        memcpy(message.payload, &notification, sizeof(notification));
        int result = ipc_send_kernel_to_owner(
            device->owner_pid, device->owner_generation,
            device->irq_capability, &message);
        if (result == 0) device->irq_notification_sent = 1U;
        else if (result != -11) (void)fence_slot(device);
    }
    end_operation();
}

int device_domain_irq_complete(int pid, uint32_t process_generation,
                               device_domain_resource_handle_t handle,
                               device_domain_irq_completion_t *completion) {
    uint32_t resource_index = 0U;
    uint32_t resource_generation = 0U;
    if (!initialized || pid <= 0 || process_generation == 0U ||
        completion == NULL || !decode_resource_handle(
            handle, &resource_index, &resource_generation)) return -22;
    if (!begin_operation()) return -16;
    resource_slot_t *resource = &resources[resource_index];
    if (resource->active == 0U ||
        resource->generation != resource_generation ||
        resource->kind != DEVICE_DOMAIN_RESOURCE_IRQ ||
        resource->owner_pid != pid ||
        resource->owner_generation != process_generation ||
        resource->device_slot >= device_count) {
        end_operation();
        return -9;
    }
    device_slot_t *device = &devices[resource->device_slot];
    if (device->generation != resource->device_generation ||
        device->state != DEVICE_DOMAIN_ACTIVE ||
        device->irq_resource != handle || device->irq_pending_count == 0U ||
        device->irq_notification_sent == 0U) {
        end_operation();
        return -22;
    }
    device_domain_irq_completion_t result = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(result),
        .resource = handle,
        .sequence = device->irq_sequence,
        .completed_count = device->irq_pending_count,
    };
    device->irq_pending_count = 0U;
    device->irq_notification_sent = 0U;
    device->irq_deadline_ms = 0U;
    if (!unmask_device_irq(device)) {
        (void)fence_slot(device);
        end_operation();
        return -5;
    }
    *completion = result;
    end_operation();
    return 0;
}

static dma_pool_slot_t *dma_pool_for_resource(resource_slot_t *resource) {
    if (resource == NULL || resource->platform_capability == 0U ||
        resource->platform_capability > DEVICE_DOMAIN_DMA_POOL_COUNT)
        return NULL;
    uint32_t pool_index = resource->platform_capability - 1U;
    dma_pool_slot_t *pool = &dma_pools[pool_index];
    if (pool->active == 0U || pool->device_slot != resource->device_slot ||
        pool->device_generation != resource->device_generation ||
        pool->owner_pid != resource->owner_pid ||
        pool->owner_generation != resource->owner_generation) return NULL;
    return pool;
}

int device_domain_dma_info(int pid, uint32_t process_generation,
                           device_domain_resource_handle_t handle,
                           device_domain_dma_info_t *info) {
    if (!initialized || pid <= 0 || process_generation == 0U || info == NULL)
        return -22;
    if (!begin_operation()) return -16;
    resource_slot_t *resource = owned_resource_locked(
        pid, process_generation, handle, DEVICE_DOMAIN_RESOURCE_DMA);
    dma_pool_slot_t *pool = dma_pool_for_resource(resource);
    if (pool == NULL) {
        end_operation();
        return resource == NULL ? -9 : -95;
    }
    *info = (device_domain_dma_info_t){
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(*info),
        .resource = handle,
        .capacity = pool->capacity,
        .alignment = 4096U,
        .direction = pool->direction,
    };
    end_operation();
    return 0;
}

int device_domain_dma_pool_stats(device_domain_dma_pool_stats_t *stats) {
    if (!initialized || stats == NULL) return -22;
    if (!begin_operation()) return -16;
    uint32_t active = 0U;
    for (uint32_t index = 0U; index < DEVICE_DOMAIN_DMA_POOL_COUNT; ++index)
        if (dma_pools[index].active != 0U) ++active;
    if (active != dma_pool_stats.active_pools ||
        dma_pool_stats.active_pools > DEVICE_DOMAIN_DMA_POOL_COUNT ||
        dma_pool_stats.peak_active_pools < dma_pool_stats.active_pools ||
        dma_pool_stats.peak_active_pools > DEVICE_DOMAIN_DMA_POOL_COUNT) {
        end_operation();
        return -84;
    }
    *stats = dma_pool_stats;
    end_operation();
    return 0;
}

static int dma_transfer_locked(int pid, uint32_t process_generation,
        device_domain_resource_handle_t handle, uint32_t offset, void *data,
        uint32_t length, bool write_to_device) {
    if (data == NULL || length == 0U ||
        length > DEVICE_DOMAIN_DMA_TRANSFER_MAX ||
        offset < DEVICE_DOMAIN_DMA_DATA_OFFSET) return -22;
    resource_slot_t *resource = owned_resource_locked(
        pid, process_generation, handle, DEVICE_DOMAIN_RESOURCE_DMA);
    dma_pool_slot_t *pool = dma_pool_for_resource(resource);
    if (pool == NULL) return resource == NULL ? -9 : -95;
    if (offset > pool->capacity || length > pool->capacity - offset)
        return -22;
    if (pool->sealed != 0U) return -16;
    const uint32_t needed = write_to_device
        ? DEVICE_DOMAIN_DMA_TO_DEVICE : DEVICE_DOMAIN_DMA_FROM_DEVICE;
    if ((pool->direction & needed) == 0U) return -13;
    device_slot_t *device = &devices[resource->device_slot];
    if (write_to_device && device->state == DEVICE_DOMAIN_ACTIVE) return -16;
    uint32_t pool_index = resource->platform_capability - 1U;
    if (write_to_device)
        memcpy(&dma_pool_storage[pool_index][offset], data, length);
    else
        memcpy(data, &dma_pool_storage[pool_index][offset], length);
    return 0;
}

int device_domain_dma_write(int pid, uint32_t process_generation,
                            device_domain_resource_handle_t handle,
                            uint32_t offset, const void *data,
                            uint32_t length) {
    if (!initialized || pid <= 0 || process_generation == 0U) return -22;
    if (!begin_operation()) return -16;
    int result = dma_transfer_locked(pid, process_generation, handle, offset,
                                     (void *)data, length, true);
    end_operation();
    return result;
}

int device_domain_dma_read(int pid, uint32_t process_generation,
                           device_domain_resource_handle_t handle,
                           uint32_t offset, void *data, uint32_t length) {
    if (!initialized || pid <= 0 || process_generation == 0U) return -22;
    if (!begin_operation()) return -16;
    int result = dma_transfer_locked(pid, process_generation, handle, offset,
                                     data, length, false);
    end_operation();
    return result;
}

int device_domain_dma_descriptor_set(
        int pid, uint32_t process_generation,
        const device_domain_dma_descriptor_t *request) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        request == NULL || request->version != DEVICE_DOMAIN_ABI_VERSION ||
        request->struct_size != sizeof(*request) || request->dma == 0U ||
        request->descriptor_index >= DEVICE_DOMAIN_DMA_DESCRIPTOR_CAPACITY ||
        request->buffer_offset < DEVICE_DOMAIN_DMA_DATA_OFFSET ||
        (request->buffer_offset &
         (DEVICE_DOMAIN_DMA_ADDRESS_ALIGNMENT - 1U)) != 0U ||
        request->length == 0U || (request->length & 3U) != 0U ||
        (request->flags & ~DEVICE_DOMAIN_DMA_DESCRIPTOR_INTERRUPT) != 0U ||
        request->reserved != 0U) return -22;
    if (!begin_operation()) return -16;
    resource_slot_t *resource = owned_resource_locked(
        pid, process_generation, request->dma, DEVICE_DOMAIN_RESOURCE_DMA);
    dma_pool_slot_t *pool = dma_pool_for_resource(resource);
    if (pool == NULL) {
        end_operation();
        return resource == NULL ? -9 : -95;
    }
    if (request->buffer_offset >= pool->capacity ||
        request->length > pool->capacity - request->buffer_offset) {
        end_operation();
        return -22;
    }
    if (pool->sealed != 0U) {
        end_operation();
        return -16;
    }
    device_slot_t *device = &devices[resource->device_slot];
    if ((pool->direction & DEVICE_DOMAIN_DMA_TO_DEVICE) == 0U) {
        end_operation();
        return -13;
    }
    if (device->state != DEVICE_DOMAIN_DMA_BOUND) {
        end_operation();
        return -16;
    }
    uint32_t pool_index = resource->platform_capability - 1U;
    uint32_t descriptor_offset = request->descriptor_index *
        DEVICE_DOMAIN_DMA_DESCRIPTOR_STRIDE;
    uint64_t address = dma_pool_physical_address(
        pool_index, request->buffer_offset);
    memcpy(&dma_pool_storage[pool_index][descriptor_offset], &address,
           sizeof(address));
    memcpy(&dma_pool_storage[pool_index][descriptor_offset + 8U],
           &request->length, sizeof(request->length));
    memcpy(&dma_pool_storage[pool_index][descriptor_offset + 12U],
           &request->flags, sizeof(request->flags));
    end_operation();
    return 0;
}

int device_domain_dma_relocate_and_seal(
        int pid, uint32_t process_generation,
        const device_domain_dma_relocation_request_t *request) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        request == NULL || request->version != DEVICE_DOMAIN_ABI_VERSION ||
        request->struct_size != sizeof(*request) || request->dma == 0U ||
        request->policy_id == 0U || request->rule_count == 0U ||
        request->rule_count > DEVICE_DOMAIN_DMA_RELOCATION_MAX_RULES ||
        request->flags != 0U || request->reserved[0] != 0U ||
        request->reserved[1] != 0U)
        return -22;
    for (uint32_t rule = request->rule_count;
         rule < DEVICE_DOMAIN_DMA_RELOCATION_MAX_RULES; ++rule)
        if (!dma_relocation_rule_zero(&request->rules[rule])) return -22;
    if (!begin_operation()) return -16;
    resource_slot_t *resource = owned_resource_locked(
        pid, process_generation, request->dma, DEVICE_DOMAIN_RESOURCE_DMA);
    dma_pool_slot_t *pool = dma_pool_for_resource(resource);
    if (pool == NULL) {
        end_operation();
        return resource == NULL ? -9 : -95;
    }
    device_slot_t *device = &devices[resource->device_slot];
    if (device->state != DEVICE_DOMAIN_DMA_BOUND || pool->sealed != 0U) {
        end_operation();
        return -16;
    }
    if (device->dma_relocation_policy_installed == 0U ||
        (pool->direction & DEVICE_DOMAIN_DMA_TO_DEVICE) == 0U) {
        end_operation();
        return -95;
    }
    const device_domain_dma_relocation_template_t *selected = NULL;
    for (uint32_t index = 0U;
         index < device->dma_relocation_policy.policy_count; ++index)
        if (device->dma_relocation_policy.policies[index].policy_id ==
            request->policy_id)
            selected = &device->dma_relocation_policy.policies[index];
    if (selected == NULL || selected->rule_count != request->rule_count) {
        end_operation();
        return -13;
    }
    uint64_t encoded[DEVICE_DOMAIN_DMA_RELOCATION_MAX_RULES] = {0};
    uint32_t pool_index = resource->platform_capability - 1U;
    for (uint32_t index = 0U; index < request->rule_count; ++index) {
        const device_domain_dma_relocation_rule_t *rule =
            &request->rules[index];
        if (!dma_relocation_rule_equal(rule, &selected->rules[index]) ||
            !dma_relocation_rule_valid(rule, pool->capacity)) {
            end_operation();
            return -13;
        }
        uint64_t current = UINT64_MAX;
        memcpy(&current,
            &dma_pool_storage[pool_index][rule->destination_pool_offset],
            sizeof(current));
        if (current != 0U) {
            end_operation();
            return -84;
        }
        const uint64_t physical = dma_pool_physical_address(
            pool_index, rule->source_pool_offset);
        if (physical > UINT32_MAX) {
            end_operation();
            return -75;
        }
        encoded[index] = (physical >> rule->shift_right) | rule->fixed_bits;
    }
    for (uint32_t index = 0U; index < request->rule_count; ++index)
        memcpy(&dma_pool_storage[pool_index]
                    [request->rules[index].destination_pool_offset],
               &encoded[index], sizeof(encoded[index]));
    pool->sealed = 1U;
    end_operation();
    return 0;
}

int device_domain_dma_vm_page_mode(
        int pid, uint32_t process_generation,
        const device_domain_dma_vm_page_mode_request_t *request) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        request == NULL || request->version != DEVICE_DOMAIN_ABI_VERSION ||
        request->struct_size != sizeof(*request) || request->device == 0U ||
        request->region == 0U || request->dma == 0U ||
        request->policy_id == 0U || request->flags != 0U ||
        request->reserved[0] != 0U || request->reserved[1] != 0U ||
        request->reserved[2] != 0U)
        return -22;
    if (!begin_operation()) return -16;
    device_slot_t *device = owned_slot(
        pid, process_generation, request->device);
    resource_slot_t *region = owned_resource_locked(
        pid, process_generation, request->region,
        DEVICE_DOMAIN_RESOURCE_REGION);
    resource_slot_t *dma = owned_resource_locked(
        pid, process_generation, request->dma, DEVICE_DOMAIN_RESOURCE_DMA);
    dma_pool_slot_t *pool = dma_pool_for_resource(dma);
    if (device == NULL || region == NULL || dma == NULL) {
        end_operation();
        return -9;
    }
    const uint32_t device_index = (uint32_t)(device - devices);
    if (region->device_slot != device_index || dma->device_slot != device_index ||
        region->device_generation != device->generation ||
        dma->device_generation != device->generation ||
        device->state != DEVICE_DOMAIN_DMA_BOUND || pool == NULL ||
        pool->sealed == 0U || device->dma_vm_page_mode_active != 0U ||
        (device->gr_prerequisite_policy_installed != 0U &&
         device->gr_prerequisite_active == 0U) ||
        device->dma_vm_page_mode_policy_installed == 0U ||
        (region->region.rights & DEVICE_DOMAIN_REGION_ACCESS_READ) == 0U) {
        end_operation();
        return -13;
    }
    const device_domain_dma_vm_page_mode_template_t *selected = NULL;
    for (uint32_t index = 0U;
         index < device->dma_vm_page_mode_policy.policy_count; ++index)
        if (device->dma_vm_page_mode_policy.policies[index].policy_id ==
            request->policy_id)
            selected = &device->dma_vm_page_mode_policy.policies[index];
    if (selected == NULL ||
        selected->region_index != region->region.region_index ||
        selected->register_offset > region->region.length_low ||
        selected->width >
            region->region.length_low - selected->register_offset) {
        end_operation();
        return -13;
    }
    uint32_t original = 0U;
    if (!platform_ops.read_region(
            &region->region, selected->register_offset, selected->width,
            &original)) {
        end_operation();
        return -5;
    }
    const uint32_t preserved = original & ~selected->writable_mask;
    const uint32_t target = preserved | selected->value;
    device->dma_vm_page_mode_active = 1U;
    device->dma_vm_page_mode_region = region->region;
    device->dma_vm_page_mode_offset = selected->register_offset;
    device->dma_vm_page_mode_width = selected->width;
    device->dma_vm_page_mode_mask = selected->writable_mask;
    device->dma_vm_page_mode_original_bits =
        original & selected->writable_mask;
    uint32_t verified = 0U;
    if (!platform_ops.write_region(
            &region->region, selected->register_offset, selected->width,
            target) ||
        !platform_ops.read_region(
            &region->region, selected->register_offset, selected->width,
            &verified) ||
        (verified & selected->writable_mask) != selected->value ||
        (verified & ~selected->writable_mask) != preserved) {
        if (!restore_dma_vm_page_mode(device)) (void)fence_slot(device);
        end_operation();
        return -5;
    }
    end_operation();
    return 0;
}

static uint32_t gr_firmware_crc32(const uint8_t *pool,
                                  const device_domain_gr_firmware_image_t *image) {
    uint32_t crc = UINT32_MAX;
    for (uint32_t index = 0U; index < image->word_count; ++index) {
        uint32_t word = 0U;
        memcpy(&word, &pool[image->pool_offset + index * sizeof(uint32_t)],
               sizeof(word));
        for (uint32_t shift = 0U; shift < 32U; shift += 8U) {
            crc ^= (word >> shift) & 0xFFU;
            for (uint32_t bit = 0U; bit < 8U; ++bit) {
                const uint32_t mask = 0U - (crc & 1U);
                crc = (crc >> 1U) ^ (0xEDB88320U & mask);
            }
        }
    }
    return ~crc;
}

static bool gr_firmware_runtime_policy_valid(
        const device_domain_gr_firmware_policy_t *policy,
        const device_domain_region_info_t *region, uint32_t pool_capacity) {
    if (policy == NULL || region == NULL ||
        policy->region_index != region->region_index ||
        policy->pmc_enable_offset > region->length_low ||
        sizeof(uint32_t) > region->length_low - policy->pmc_enable_offset)
        return false;
    for (uint32_t index = 0U;
         index < DEVICE_DOMAIN_GR_FIRMWARE_IMAGE_COUNT; ++index) {
        const device_domain_gr_firmware_image_t *image =
            &policy->images[index];
        const uint32_t bytes = image->word_count * sizeof(uint32_t);
        if (image->pool_offset > pool_capacity ||
            bytes > pool_capacity - image->pool_offset ||
            image->falcon_base > region->length_low ||
            0x1C8U > region->length_low - image->falcon_base)
            return false;
    }
    return true;
}

static bool gr_firmware_scrub_complete(
        const device_domain_region_info_t *region,
        const device_domain_gr_firmware_policy_t *policy) {
    const uint64_t started = platform_ops.monotonic_ms();
    const uint64_t deadline = started > UINT64_MAX -
            DEVICE_DOMAIN_GR_FIRMWARE_SCRUB_TIMEOUT_MS
        ? UINT64_MAX
        : started + DEVICE_DOMAIN_GR_FIRMWARE_SCRUB_TIMEOUT_MS;
    for (uint32_t attempt = 0U;
         attempt < DEVICE_DOMAIN_GR_FIRMWARE_SCRUB_TIMEOUT_MS; ++attempt) {
        uint32_t fecs = 0U;
        uint32_t gpccs = 0U;
        if (!platform_ops.read_region(region,
                policy->images[0].falcon_base + 0x10CU,
                sizeof(uint32_t), &fecs) ||
            !platform_ops.read_region(region,
                policy->images[2].falcon_base + 0x10CU,
                sizeof(uint32_t), &gpccs))
            return false;
        if (((fecs | gpccs) & 0x00000006U) == 0U) return true;
        const uint64_t now = platform_ops.monotonic_ms();
        if (now < started || now >= deadline) return false;
#ifndef REIST_HOST_TEST
        if (scheduler_sleep_ms(1U) != 0) return false;
#endif
    }
    return false;
}

static bool gr_firmware_upload_image(
        const device_domain_region_info_t *region, const uint8_t *pool,
        const device_domain_gr_firmware_image_t *image) {
    const uint32_t control_offset = image->falcon_base +
        (image->memory_kind == DEVICE_DOMAIN_GR_FIRMWARE_IMEM
            ? 0x180U : 0x1C0U);
    const uint32_t data_offset = control_offset + 4U;
    const uint32_t tag_offset = control_offset + 8U;
    if (!platform_ops.write_region(
            region, control_offset, sizeof(uint32_t), 0x01000000U))
        return false;
    for (uint32_t index = 0U; index < image->word_count; ++index) {
        uint32_t word = 0U;
        memcpy(&word, &pool[image->pool_offset + index * sizeof(uint32_t)],
               sizeof(word));
        if (image->memory_kind == DEVICE_DOMAIN_GR_FIRMWARE_IMEM &&
            (index & 0x3FU) == 0U &&
            !platform_ops.write_region(region, tag_offset, sizeof(uint32_t),
                                       index >> 6U))
            return false;
        if (!platform_ops.write_region(
                region, data_offset, sizeof(uint32_t), word))
            return false;
    }
    if (!platform_ops.write_region(
            region, control_offset, sizeof(uint32_t), 0x02000000U))
        return false;
    for (uint32_t index = 0U; index < image->word_count; ++index) {
        uint32_t expected = 0U;
        uint32_t actual = 0U;
        memcpy(&expected,
            &pool[image->pool_offset + index * sizeof(uint32_t)],
            sizeof(expected));
        if (!platform_ops.read_region(
                region, data_offset, sizeof(uint32_t), &actual) ||
            actual != expected)
            return false;
    }
    return true;
}

int device_domain_gr_firmware_upload(
        int pid, uint32_t process_generation,
        const device_domain_gr_firmware_request_t *request) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        request == NULL || request->version != DEVICE_DOMAIN_ABI_VERSION ||
        request->struct_size != sizeof(*request) || request->device == 0U ||
        request->region == 0U || request->dma == 0U ||
        request->policy_id == 0U || request->flags != 0U ||
        request->reserved[0] != 0U || request->reserved[1] != 0U ||
        request->reserved[2] != 0U)
        return -22;
    if (!begin_operation()) return -16;
    device_slot_t *device = owned_slot(
        pid, process_generation, request->device);
    resource_slot_t *region = owned_resource_locked(
        pid, process_generation, request->region,
        DEVICE_DOMAIN_RESOURCE_REGION);
    resource_slot_t *dma = owned_resource_locked(
        pid, process_generation, request->dma, DEVICE_DOMAIN_RESOURCE_DMA);
    dma_pool_slot_t *pool = dma_pool_for_resource(dma);
    if (device == NULL || region == NULL || dma == NULL) {
        end_operation();
        return -9;
    }
    const uint32_t device_index = (uint32_t)(device - devices);
    const device_domain_gr_firmware_policy_t *policy =
        &device->gr_firmware_policy;
    if (region->device_slot != device_index || dma->device_slot != device_index ||
        region->device_generation != device->generation ||
        dma->device_generation != device->generation ||
        device->state != DEVICE_DOMAIN_DMA_BOUND || pool == NULL ||
        pool->sealed == 0U || device->dma_vm_page_mode_active == 0U ||
        (device->gr_prerequisite_policy_installed != 0U &&
         device->gr_prerequisite_active == 0U) ||
        device->gr_firmware_policy_installed == 0U ||
        device->gr_firmware_active != 0U ||
        request->policy_id != policy->policy_id ||
        (region->region.rights & DEVICE_DOMAIN_REGION_ACCESS_READ) == 0U ||
        !gr_firmware_runtime_policy_valid(
            policy, &region->region, pool->capacity)) {
        end_operation();
        return -13;
    }
    const uint32_t pool_index = dma->platform_capability - 1U;
    const uint8_t *storage = dma_pool_storage[pool_index];
    for (uint32_t index = 0U;
         index < DEVICE_DOMAIN_GR_FIRMWARE_IMAGE_COUNT; ++index)
        if (gr_firmware_crc32(storage, &policy->images[index]) !=
                policy->images[index].crc32) {
            end_operation();
            return -84;
        }
    uint32_t pmc_enable = 0U;
    if (!platform_ops.read_region(&region->region,
            policy->pmc_enable_offset, sizeof(uint32_t), &pmc_enable) ||
        (pmc_enable & policy->pmc_gr_mask) != policy->pmc_gr_mask) {
        end_operation();
        return -13;
    }
    device->gr_firmware_active = 1U;
    device->gr_firmware_region = region->region;
    device->gr_firmware_pmc_offset = policy->pmc_enable_offset;
    device->gr_firmware_pmc_mask = policy->pmc_gr_mask;
    device->gr_firmware_pmc_original_bits =
        pmc_enable & policy->pmc_gr_mask;
    bool uploaded = toggle_gr_reset(&region->region,
            policy->pmc_enable_offset, policy->pmc_gr_mask,
            device->gr_firmware_pmc_original_bits) &&
        gr_firmware_scrub_complete(&region->region, policy);
    for (uint32_t index = 0U;
         uploaded && index < DEVICE_DOMAIN_GR_FIRMWARE_IMAGE_COUNT; ++index)
        uploaded = gr_firmware_upload_image(
            &region->region, storage, &policy->images[index]);
    if (!uploaded) {
        if (!reset_gr_firmware_state(device)) (void)fence_slot(device);
        end_operation();
        return -5;
    }
    end_operation();
    return 0;
}

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t gpc_count;
    uint32_t rop_count;
    uint32_t tpc_total;
    uint32_t tpc_max;
    uint32_t tpc_count[DEVICE_DOMAIN_GR_MAX_GPCS];
    uint32_t ppc_tpc_mask[DEVICE_DOMAIN_GR_MAX_GPCS];
    uint32_t reserved[2];
} gr_prerequisite_topology_t;

typedef struct {
    uint32_t mmu_read_offset;
    uint32_t mmu_write_offset;
    uint32_t tag_offset;
    uint32_t tag_bytes;
    uint32_t tag_base;
    uint32_t vram_bytes;
    uint32_t ltc_count;
    uint32_t lts_count;
    uint32_t tag_count;
} gr_prerequisite_plan_t;

typedef struct {
    uint32_t pagepool_offset;
    uint32_t bundle_offset;
    uint32_t attrib_offset;
    uint32_t attrib_bytes;
    uint32_t golden_offset;
    uint32_t golden_bytes;
    uint32_t total_bytes;
} gr_context_memory_plan_t;

_Static_assert(sizeof(gr_prerequisite_topology_t) ==
                   GK208_GR_PLAN_STRUCT_BYTES,
               "GK208 GR prerequisite topology changed");

static uint32_t gr_crc32_word(uint32_t crc, uint32_t word) {
    for (uint32_t shift = 0U; shift < 32U; shift += 8U) {
        crc ^= (word >> shift) & 0xFFU;
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t polynomial_mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & polynomial_mask);
        }
    }
    return crc;
}

static uint32_t gr_topology_crc32(
        const gr_prerequisite_topology_t *topology) {
    uint32_t crc = UINT32_MAX;
    crc = gr_crc32_word(crc, topology->version);
    crc = gr_crc32_word(crc, topology->struct_size);
    crc = gr_crc32_word(crc, topology->gpc_count);
    crc = gr_crc32_word(crc, topology->rop_count);
    crc = gr_crc32_word(crc, topology->tpc_total);
    crc = gr_crc32_word(crc, topology->tpc_max);
    for (uint32_t index = 0U; index < DEVICE_DOMAIN_GR_MAX_GPCS; ++index)
        crc = gr_crc32_word(crc, topology->tpc_count[index]);
    for (uint32_t index = 0U; index < DEVICE_DOMAIN_GR_MAX_GPCS; ++index)
        crc = gr_crc32_word(crc, topology->ppc_tpc_mask[index]);
    crc = gr_crc32_word(crc, topology->reserved[0]);
    crc = gr_crc32_word(crc, topology->reserved[1]);
    return ~crc;
}

static bool gr_read32(const device_domain_region_info_t *region,
                      uint32_t offset, uint32_t *value) {
    return region != NULL && value != NULL && (offset & 3U) == 0U &&
        offset <= region->length_low &&
        sizeof(uint32_t) <= region->length_low - offset &&
        platform_ops.read_region(region, offset, sizeof(uint32_t), value);
}

static bool gr_sample_topology(
        const device_domain_region_info_t *region,
        gr_prerequisite_topology_t *topology) {
    if (region == NULL || topology == NULL) return false;
    memset(topology, 0, sizeof(*topology));
    topology->version = GK208_GR_PLAN_VERSION;
    topology->struct_size = sizeof(*topology);
    uint32_t summary = 0U;
    if (!gr_read32(region, GK208_GR_TOPOLOGY_REGISTER, &summary))
        return false;
    topology->gpc_count = summary & 0x1FU;
    topology->rop_count = (summary >> 16U) & 0x1FU;
    if (topology->gpc_count == 0U ||
        topology->gpc_count > DEVICE_DOMAIN_GR_MAX_GPCS ||
        topology->rop_count == 0U ||
        topology->rop_count > GK208_GR_MAX_ROPS)
        return false;
    for (uint32_t gpc = 0U; gpc < topology->gpc_count; ++gpc) {
        const uint32_t base = GK208_GR_GPC_UNIT_BASE +
            gpc * GK208_GR_GPC_UNIT_STRIDE;
        uint32_t tpcs = 0U;
        uint32_t mask = 0U;
        if (!gr_read32(region, base + GK208_GR_GPC_TPC_COUNT_OFFSET,
                &tpcs) ||
            !gr_read32(region, base + GK208_GR_GPC_PPC_MASK_OFFSET,
                &mask) ||
            tpcs == 0U || tpcs > GK208_GR_MAX_TPCS_PER_GPC ||
            mask == 0U || (mask >> tpcs) != 0U ||
            topology->tpc_total > GK208_GR_MAX_TOTAL_TPCS - tpcs)
            return false;
        uint32_t bits = 0U;
        for (uint32_t value = mask; value != 0U; value >>= 1U)
            bits += value & 1U;
        if (bits != tpcs) return false;
        topology->tpc_count[gpc] = tpcs;
        topology->ppc_tpc_mask[gpc] = mask;
        topology->tpc_total += tpcs;
        if (topology->tpc_max < tpcs) topology->tpc_max = tpcs;
    }
    return topology->tpc_total != 0U &&
        topology->tpc_total <= GK208_GR_MAX_TOTAL_TPCS &&
        topology->tpc_max != 0U;
}

static bool gr_operation_address_valid(
        const device_domain_region_info_t *region, uint32_t address) {
    return region != NULL && (address & 3U) == 0U &&
        address <= region->length_low &&
        sizeof(uint32_t) <= region->length_low - address;
}

static bool gr_execution_image_valid(
        const uint8_t *storage, uint32_t capacity,
        const device_domain_gr_prerequisite_policy_t *policy,
        const device_domain_region_info_t *region,
        const gr_prerequisite_topology_t *topology,
        uint32_t *image_crc_out) {
    if (storage == NULL || policy == NULL || region == NULL ||
        topology == NULL || image_crc_out == NULL ||
        policy->execution_pool_offset > capacity ||
        DEVICE_DOMAIN_GR_EXECUTION_HEADER_BYTES >
            capacity - policy->execution_pool_offset)
        return false;
    device_domain_gr_execution_header_t header;
    memcpy(&header, storage + policy->execution_pool_offset, sizeof(header));
    if (header.version != GK208_GR_EXECUTION_VERSION ||
        header.header_size != DEVICE_DOMAIN_GR_EXECUTION_HEADER_BYTES ||
        header.operation_count == 0U ||
        header.operation_count > policy->execution_max_operations ||
        header.operation_count > DEVICE_DOMAIN_GR_EXECUTION_OP_CAPACITY ||
        header.flags != policy->execution_flags ||
        header.gpc_count != topology->gpc_count ||
        header.tpc_total != topology->tpc_total ||
        header.rop_count != topology->rop_count ||
        header.topology_crc32 != gr_topology_crc32(topology) ||
        header.vram_relocation_count != 2U ||
        header.static_mmio_operation_count == 0U ||
        header.zbc_operation_count == 0U ||
        header.context_operation_count == 0U ||
        header.reserved[0] != 0U || header.reserved[1] != 0U)
        return false;
    const uint32_t operations_bytes =
        header.operation_count * DEVICE_DOMAIN_GR_EXECUTION_OP_BYTES;
    const uint32_t used = DEVICE_DOMAIN_GR_EXECUTION_HEADER_BYTES +
        operations_bytes;
    if (header.used_bytes != used ||
        used > capacity - policy->execution_pool_offset ||
        header.static_mmio_operation_count > header.operation_count ||
        header.zbc_operation_count > header.operation_count ||
        header.context_operation_count > header.operation_count ||
        header.static_mmio_operation_count + header.zbc_operation_count >
            header.operation_count - header.context_operation_count)
        return false;

    uint32_t crc = UINT32_MAX;
    uint32_t vram_mask = 0U;
    uint32_t idle_waits = 0U;
    uint32_t context_groups = 0U;
    uint32_t context_transfers = 0U;
    uint32_t hub_command = 0U;
    uint32_t hub_start = 0U;
    uint32_t ready_waits = 0U;
    uint32_t size_reads = 0U;
    uint32_t context_base = 0U;
    uint32_t context_remaining = 0U;
    uint32_t context_stage = 0U;
    const uint8_t *operations = storage + policy->execution_pool_offset +
        DEVICE_DOMAIN_GR_EXECUTION_HEADER_BYTES;
    for (uint32_t index = 0U; index < header.operation_count; ++index) {
        device_domain_gr_execution_op_t operation;
        memcpy(&operation,
               operations + index * DEVICE_DOMAIN_GR_EXECUTION_OP_BYTES,
               sizeof(operation));
        crc = gr_crc32_word(crc, operation.opcode);
        crc = gr_crc32_word(crc, operation.address);
        crc = gr_crc32_word(crc, operation.value);
        crc = gr_crc32_word(crc, operation.mask);
        if (context_remaining != 0U &&
            operation.opcode != DEVICE_DOMAIN_GR_OP_CONTEXT_TRANSFER)
            return false;
        switch (operation.opcode) {
        case DEVICE_DOMAIN_GR_OP_WRITE32:
            if (!gr_operation_address_valid(region, operation.address) ||
                operation.mask != 0U) return false;
            if (operation.address == 0x0040910CU && operation.value == 0U) {
                if (context_stage != 1U || context_remaining != 0U ||
                    context_groups != 5U)
                    return false;
                context_stage = 2U;
                ++hub_command;
            } else if (operation.address == 0x00409100U &&
                       operation.value == 2U) {
                if (context_stage != 2U) return false;
                context_stage = 3U;
                ++hub_start;
            } else if (context_stage != 0U) {
                return false;
            }
            break;
        case DEVICE_DOMAIN_GR_OP_MASK32:
            if (context_stage != 0U) return false;
            if (!gr_operation_address_valid(region, operation.address) ||
                operation.mask == 0U ||
                (operation.value & ~operation.mask) != 0U) return false;
            break;
        case DEVICE_DOMAIN_GR_OP_COPY_MASKED32:
            if (context_stage != 0U) return false;
            if (!gr_operation_address_valid(region, operation.address) ||
                !gr_operation_address_valid(region, operation.value) ||
                operation.mask == 0U) return false;
            break;
        case DEVICE_DOMAIN_GR_OP_VRAM_OFFSET32:
            if (context_stage != 0U) return false;
            if (operation.address == 0x004188B4U &&
                operation.value == 1U && operation.mask == 8U)
                vram_mask |= 1U;
            else if (operation.address == 0x004188B8U &&
                     operation.value == 2U && operation.mask == 8U)
                vram_mask |= 2U;
            else
                return false;
            break;
        case DEVICE_DOMAIN_GR_OP_WAIT_IDLE:
            if (context_stage != 0U) return false;
            if (operation.address != 0x00400700U ||
                operation.value != 0x0040060CU ||
                operation.mask != 2000U) return false;
            ++idle_waits;
            break;
        case DEVICE_DOMAIN_GR_OP_CONTEXT_GROUP:
            if (context_remaining != 0U ||
                (context_stage != 0U && context_stage != 1U) ||
                (operation.address != 0x00409000U &&
                 operation.address != 0x0041A000U) ||
                (operation.value != 0U && operation.value != 4U &&
                 operation.value != 8U) ||
                operation.mask == 0U || operation.mask > 256U)
                return false;
            context_base = operation.address;
            context_remaining = operation.mask;
            context_stage = 1U;
            ++context_groups;
            break;
        case DEVICE_DOMAIN_GR_OP_CONTEXT_TRANSFER:
            if ((operation.address != 0x004091C4U &&
                 operation.address != 0x0041A1C4U) || operation.mask != 0U ||
                context_remaining == 0U ||
                operation.address != context_base + 0x1C4U)
                return false;
            --context_remaining;
            ++context_transfers;
            break;
        case DEVICE_DOMAIN_GR_OP_WAIT_MASK32:
            if (context_stage != 3U ||
                operation.address != 0x00409800U ||
                operation.value != 0x80000000U ||
                operation.mask != 2000U) return false;
            context_stage = 4U;
            ++ready_waits;
            break;
        case DEVICE_DOMAIN_GR_OP_READ32_NONZERO:
            if (context_stage != 4U ||
                operation.address != 0x00409804U || operation.value != 0U ||
                operation.mask != 0U || index + 1U != header.operation_count)
                return false;
            context_stage = 5U;
            ++size_reads;
            break;
        default:
            return false;
        }
    }
    crc = ~crc;
    if (crc != header.operation_crc32 || vram_mask != 3U ||
        idle_waits != 1U || context_groups != 5U ||
        context_remaining != 0U ||
        context_stage != 5U ||
        context_transfers == 0U || hub_command != 1U || hub_start != 1U ||
        ready_waits != 1U || size_reads != 1U ||
        header.context_operation_count !=
            context_groups + context_transfers + 4U)
        return false;
    *image_crc_out = crc;
    return true;
}

static bool gr_align_up_u64(uint64_t value, uint32_t alignment,
                            uint64_t *result) {
    if (result == NULL || alignment == 0U ||
        (alignment & (alignment - 1U)) != 0U ||
        value > UINT64_MAX - (alignment - 1U))
        return false;
    *result = (value + alignment - 1U) & ~(uint64_t)(alignment - 1U);
    return true;
}

static bool gr_build_prerequisite_plan(
        const device_domain_region_info_t *region,
        const device_domain_gr_prerequisite_policy_t *policy,
        gr_prerequisite_plan_t *plan) {
    uint32_t fbp_count = 0U;
    uint32_t fbpa_count = 0U;
    uint32_t disable_mask = 0U;
    uint32_t slice_value = 0U;
    uint32_t page_config = 0U;
    if (region == NULL || policy == NULL || plan == NULL ||
        !gr_read32(region, GK208_FB_PART_COUNT, &fbp_count) ||
        !gr_read32(region, GK208_FB_FBPA_COUNT, &fbpa_count) ||
        !gr_read32(region, GK208_FB_DISABLE_MASK, &disable_mask) ||
        !gr_read32(region, GK208_LTC_SLICE_COUNT, &slice_value) ||
        !gr_read32(region, GK208_FB_PAGE_CONFIG, &page_config) ||
        fbp_count == 0U || fbp_count > policy->fbp_max ||
        fbpa_count < fbp_count || fbpa_count > policy->fbpa_max ||
        (fbpa_count % fbp_count) != 0U)
        return false;
    uint32_t ltc_count = 0U;
    for (uint32_t fbp = 0U; fbp < fbp_count; ++fbp)
        if ((disable_mask & (1U << fbp)) == 0U) ++ltc_count;
    const uint32_t lts_count = slice_value >> 28U;
    if (ltc_count == 0U || lts_count == 0U || lts_count > 4U)
        return false;

    uint64_t total_mib = 0U;
    for (uint32_t fbpa = 0U; fbpa < fbpa_count; ++fbpa) {
        if ((disable_mask & (1U << fbpa)) != 0U) continue;
        uint32_t amount_mib = 0U;
        if (!gr_read32(region,
                GK208_FB_FBPA_SIZE_BASE + fbpa * GK208_FB_FBPA_SIZE_STRIDE,
                &amount_mib) || amount_mib == 0U || amount_mib > 4095U ||
            total_mib > 4095U - amount_mib)
            return false;
        total_mib += amount_mib;
    }
    const uint64_t vram_bytes = total_mib << 20U;
    if (vram_bytes <= GK208_VRAM_VGA_HEAD_BYTES +
            GK208_VRAM_VBIOS_TAIL_BYTES || vram_bytes > UINT32_MAX)
        return false;

    uint64_t tag_count = (vram_bytes >> 17U) / 4U;
    const uint32_t tag_bits = (page_config & 0x00001000U) != 0U ? 16U : 17U;
    if (tag_count > (1U << tag_bits)) tag_count = 1U << tag_bits;
    tag_count = (tag_count + 63U) & ~63ULL;
    const uint64_t tag_alignment =
        (uint64_t)ltc_count * GK208_LTC_TAG_ALIGN_PER_LTC;
    const uint64_t tag_margin = tag_alignment < GK208_LTC_TAG_MIN_MARGIN
        ? GK208_LTC_TAG_MIN_MARGIN : tag_alignment;
    uint64_t tag_bytes = (tag_count / 64U) * GK208_LTC_TAG_BLOCK_BYTES +
        tag_margin + tag_alignment;
    if (!gr_align_up_u64(tag_bytes, 4096U, &tag_bytes)) return false;

    uint64_t scanout_end =
        (uint64_t)policy->scanout_offset + policy->scanout_bytes;
    if (scanout_end < GK208_VRAM_VGA_HEAD_BYTES)
        scanout_end = GK208_VRAM_VGA_HEAD_BYTES;
    uint64_t mmu_read = 0U;
    if (!gr_align_up_u64(scanout_end, policy->fault_buffer_alignment,
            &mmu_read)) return false;
    const uint64_t mmu_write = mmu_read + policy->fault_buffer_bytes;
    uint64_t tag_offset = 0U;
    if (!gr_align_up_u64(mmu_write + policy->fault_buffer_bytes, 4096U,
            &tag_offset)) return false;
    const uint64_t allocation_end = tag_offset + tag_bytes;
    uint64_t usable_end = vram_bytes - GK208_VRAM_VBIOS_TAIL_BYTES;
    if (usable_end > policy->vram_aperture_bytes)
        usable_end = policy->vram_aperture_bytes;
    if (allocation_end < tag_offset || allocation_end > usable_end ||
        mmu_read > UINT32_MAX || mmu_write > UINT32_MAX ||
        tag_offset > UINT32_MAX || tag_bytes > UINT32_MAX)
        return false;
    const uint64_t tag_address = tag_offset + tag_margin;
    const uint64_t tag_base =
        (tag_address + tag_alignment - 1U) / tag_alignment;
    if (tag_base == 0U || tag_base > UINT32_MAX) return false;
    *plan = (gr_prerequisite_plan_t){
        .mmu_read_offset = (uint32_t)mmu_read,
        .mmu_write_offset = (uint32_t)mmu_write,
        .tag_offset = (uint32_t)tag_offset,
        .tag_bytes = (uint32_t)tag_bytes,
        .tag_base = (uint32_t)tag_base,
        .vram_bytes = (uint32_t)vram_bytes,
        .ltc_count = ltc_count,
        .lts_count = lts_count,
        .tag_count = (uint32_t)tag_count,
    };
    return true;
}

static bool gr_build_context_memory_plan(
        const gr_prerequisite_topology_t *topology,
        const gr_prerequisite_plan_t *prerequisite,
        const device_domain_gr_prerequisite_policy_t *policy,
        uint32_t context_size, gr_context_memory_plan_t *plan) {
    if (topology == NULL || prerequisite == NULL || policy == NULL ||
        plan == NULL || topology->tpc_total == 0U || context_size == 0U)
        return false;
    const uint64_t attrib_bytes =
        (uint64_t)GK208_GR_ATTRIB_STRIDE * GK208_GR_ATTRIB_TOTAL_MAX *
        topology->tpc_total;
    uint64_t aligned_context = 0U;
    uint64_t cursor =
        (uint64_t)prerequisite->tag_offset + prerequisite->tag_bytes;
    if (attrib_bytes == 0U || attrib_bytes > UINT32_MAX ||
        !gr_align_up_u64(context_size, GK208_GR_GOLDEN_ALIGNMENT,
            &aligned_context) ||
        aligned_context > UINT32_MAX - GK208_GR_GOLDEN_CB_RESERVED ||
        cursor < prerequisite->tag_offset ||
        !gr_align_up_u64(cursor, GK208_GR_PAGEPOOL_ALIGNMENT, &cursor) ||
        cursor > UINT32_MAX)
        return false;
    const uint64_t pagepool_offset = cursor;
    cursor += GK208_GR_PAGEPOOL_BYTES;
    if (cursor < pagepool_offset ||
        !gr_align_up_u64(cursor, GK208_GR_BUNDLE_ALIGNMENT, &cursor) ||
        cursor > UINT32_MAX)
        return false;
    const uint64_t bundle_offset = cursor;
    cursor += GK208_GR_BUNDLE_BYTES;
    if (cursor < bundle_offset ||
        !gr_align_up_u64(cursor, GK208_GR_ATTRIB_ALIGNMENT, &cursor) ||
        cursor > UINT32_MAX)
        return false;
    const uint64_t attrib_offset = cursor;
    cursor += attrib_bytes;
    if (cursor < attrib_offset ||
        !gr_align_up_u64(cursor, GK208_GR_GOLDEN_ALIGNMENT, &cursor) ||
        cursor > UINT32_MAX)
        return false;
    const uint64_t golden_offset = cursor;
    const uint64_t golden_bytes =
        GK208_GR_GOLDEN_CB_RESERVED + aligned_context;
    cursor += golden_bytes;
    uint64_t usable_end = prerequisite->vram_bytes -
        GK208_VRAM_VBIOS_TAIL_BYTES;
    if (usable_end > policy->vram_aperture_bytes)
        usable_end = policy->vram_aperture_bytes;
    if (cursor < golden_offset || cursor > usable_end ||
        cursor > UINT32_MAX || cursor < pagepool_offset ||
        cursor - pagepool_offset > UINT32_MAX)
        return false;
    *plan = (gr_context_memory_plan_t){
        .pagepool_offset = (uint32_t)pagepool_offset,
        .bundle_offset = (uint32_t)bundle_offset,
        .attrib_offset = (uint32_t)attrib_offset,
        .attrib_bytes = (uint32_t)attrib_bytes,
        .golden_offset = (uint32_t)golden_offset,
        .golden_bytes = (uint32_t)golden_bytes,
        .total_bytes = (uint32_t)(cursor - pagepool_offset),
    };
    return true;
}

int device_domain_gr_prerequisites(
        int pid, uint32_t process_generation,
        const device_domain_gr_prerequisite_request_t *request) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        request == NULL || request->version != DEVICE_DOMAIN_ABI_VERSION ||
        request->struct_size != sizeof(*request) || request->device == 0U ||
        request->region == 0U || request->dma == 0U ||
        request->policy_id == 0U || request->flags != 0U ||
        request->reserved[0] != 0U || request->reserved[1] != 0U ||
        request->reserved[2] != 0U)
        return -22;
    if (!begin_operation()) return -16;
    device_slot_t *device = owned_slot(
        pid, process_generation, request->device);
    resource_slot_t *region = owned_resource_locked(
        pid, process_generation, request->region,
        DEVICE_DOMAIN_RESOURCE_REGION);
    resource_slot_t *dma = owned_resource_locked(
        pid, process_generation, request->dma, DEVICE_DOMAIN_RESOURCE_DMA);
    dma_pool_slot_t *pool = dma_pool_for_resource(dma);
    if (device == NULL || region == NULL || dma == NULL) {
        end_operation();
        return -9;
    }
    const uint32_t device_index = (uint32_t)(device - devices);
    const device_domain_gr_prerequisite_policy_t *policy =
        &device->gr_prerequisite_policy;
    if (region->device_slot != device_index || dma->device_slot != device_index ||
        region->device_generation != device->generation ||
        dma->device_generation != device->generation ||
        device->state != DEVICE_DOMAIN_DMA_BOUND || pool == NULL ||
        pool->sealed == 0U || device->dma_vm_page_mode_active != 0U ||
        device->gr_firmware_active != 0U ||
        device->gr_prerequisite_policy_installed == 0U ||
        device->gr_prerequisite_active != 0U ||
        request->policy_id != policy->policy_id ||
        region->region.region_index != policy->region_index ||
        (region->region.rights & DEVICE_DOMAIN_REGION_ACCESS_READ) == 0U) {
        end_operation();
        return -13;
    }
    const uint32_t pool_index = dma->platform_capability - 1U;
    const uint8_t *storage = dma_pool_storage[pool_index];
    gr_prerequisite_topology_t first;
    gr_prerequisite_topology_t second;
    uint32_t image_crc = 0U;
    gr_prerequisite_plan_t plan;
    bool valid = gr_sample_topology(&region->region, &first) &&
        gr_execution_image_valid(storage, pool->capacity, policy,
            &region->region, &first, &image_crc) &&
        gr_build_prerequisite_plan(&region->region, policy, &plan) &&
        gr_sample_topology(&region->region, &second) &&
        memcmp(&first, &second, sizeof(first)) == 0;
    if (!valid) {
        end_operation();
        return -84;
    }
    device->gr_prerequisite_active = 1U;
    device->gr_prerequisite_image_crc = image_crc;
    device->gr_prerequisite_topology_crc = gr_topology_crc32(&first);
    device->gr_prerequisite_mmu_read_offset = plan.mmu_read_offset;
    device->gr_prerequisite_mmu_write_offset = plan.mmu_write_offset;
    device->gr_prerequisite_tag_offset = plan.tag_offset;
    device->gr_prerequisite_tag_bytes = plan.tag_bytes;
    device->gr_prerequisite_tag_base = plan.tag_base;
    device->gr_prerequisite_vram_bytes = plan.vram_bytes;
    device->gr_prerequisite_ltc_count = plan.ltc_count;
    device->gr_prerequisite_lts_count = plan.lts_count;
    device->gr_prerequisite_tag_count = plan.tag_count;
    end_operation();
    return 0;
}

static bool gr_plan_matches_device(
        const gr_prerequisite_plan_t *plan, const device_slot_t *device) {
    return plan != NULL && device != NULL &&
        plan->mmu_read_offset == device->gr_prerequisite_mmu_read_offset &&
        plan->mmu_write_offset == device->gr_prerequisite_mmu_write_offset &&
        plan->tag_offset == device->gr_prerequisite_tag_offset &&
        plan->tag_bytes == device->gr_prerequisite_tag_bytes &&
        plan->tag_base == device->gr_prerequisite_tag_base &&
        plan->vram_bytes == device->gr_prerequisite_vram_bytes &&
        plan->ltc_count == device->gr_prerequisite_ltc_count &&
        plan->lts_count == device->gr_prerequisite_lts_count &&
        plan->tag_count == device->gr_prerequisite_tag_count;
}

static bool gr_deadline_valid(uint64_t started, uint64_t deadline) {
    const uint64_t now = platform_ops.monotonic_ms();
    return now >= started && now < deadline;
}

static bool gr_wait_one_ms(uint64_t started, uint64_t deadline) {
    if (!gr_deadline_valid(started, deadline)) return false;
#ifndef REIST_HOST_TEST
    if (scheduler_sleep_ms(1U) != 0 && scheduler_yield() != 0) return false;
#endif
    return true;
}

static int gr_zero_vram_buffer(
        const device_domain_region_info_t *vram, uint32_t offset,
        uint32_t bytes, uint64_t started, uint64_t deadline) {
    if (vram == NULL || bytes == 0U || (offset & 3U) != 0U ||
        (bytes & 3U) != 0U || vram->base_high != 0U ||
        offset > vram->length_low || bytes > vram->length_low - offset ||
        vram->base_low > UINT32_MAX - offset)
        return -84;
    device_domain_region_info_t window = *vram;
    window.base_low += offset;
    window.length_low = bytes;
    window.length_high = 0U;
    if (!platform_ops.prepare_region(&window)) return -5;
    for (uint32_t cursor = 0U; cursor < bytes; cursor += 4U) {
        if ((cursor & 0x3FFU) == 0U &&
            !gr_deadline_valid(started, deadline))
            return -110;
        if (!platform_ops.write_region(
                &window, cursor, sizeof(uint32_t), 0U))
            return -5;
    }
    return 0;
}

static int gr_wait_mask(
        const device_domain_region_info_t *region, uint32_t offset,
        uint32_t mask, uint32_t expected, uint32_t timeout_ms,
        uint64_t started, uint64_t deadline) {
    if (region == NULL || mask == 0U || timeout_ms == 0U ||
        timeout_ms > DEVICE_DOMAIN_GR_EXECUTION_TIMEOUT_MS)
        return -84;
    for (uint32_t attempt = 0U; attempt < timeout_ms; ++attempt) {
        uint32_t value = 0U;
        if (!gr_read32(region, offset, &value)) return -5;
        if ((value & mask) == expected) return 0;
        if (!gr_wait_one_ms(started, deadline)) return -110;
    }
    return -110;
}

static int gr_wait_idle(
        const device_domain_region_info_t *region,
        const device_domain_gr_execution_op_t *operation,
        uint64_t started, uint64_t deadline) {
    for (uint32_t attempt = 0U; attempt < operation->mask; ++attempt) {
        uint32_t update = 0U;
        uint32_t busy = 0U;
        if (!gr_read32(region, operation->address, &update) ||
            !gr_read32(region, operation->value, &busy))
            return -5;
        if ((busy & 1U) == 0U) return 0;
        if (!gr_wait_one_ms(started, deadline)) return -110;
    }
    return -110;
}

static int gr_initialize_ltc(
        const device_domain_region_info_t *region,
        const gr_prerequisite_plan_t *plan,
        uint64_t started, uint64_t deadline) {
    uint32_t page = 0U;
    uint32_t mode = 0U;
    if (region == NULL || plan == NULL || plan->ltc_count == 0U ||
        plan->lts_count == 0U || plan->tag_count == 0U ||
        !gr_read32(region, GK208_FB_PAGE_CONFIG, &page) ||
        !gr_read32(region, GK208_LTC_PAGE_MODE, &mode))
        return -5;
    const uint32_t lpg128 = (page & 1U) == 0U ? 2U : 0U;
    const uint32_t target_mode = (mode & ~2U) | lpg128;
    if (!platform_ops.write_region(region, GK208_LTC_COUNT,
            sizeof(uint32_t), plan->ltc_count) ||
        !platform_ops.write_region(region, GK208_LTC_ACTIVE_COUNT,
            sizeof(uint32_t), plan->ltc_count) ||
        !platform_ops.write_region(region, GK208_LTC_TAG_BASE,
            sizeof(uint32_t), plan->tag_base) ||
        !platform_ops.write_region(region, GK208_LTC_PAGE_MODE,
            sizeof(uint32_t), target_mode) ||
        !platform_ops.write_region(region, GK208_LTC_CBC_START,
            sizeof(uint32_t), 0U) ||
        !platform_ops.write_region(region, GK208_LTC_CBC_LIMIT,
            sizeof(uint32_t), plan->tag_count - 1U) ||
        !platform_ops.write_region(region, GK208_LTC_CBC_COMMAND,
            sizeof(uint32_t), GK208_LTC_CBC_CLEAR_COMMAND))
        return -5;
    for (uint32_t ltc = 0U; ltc < plan->ltc_count; ++ltc) {
        for (uint32_t slice = 0U; slice < plan->lts_count; ++slice) {
            const uint32_t status = GK208_LTC_CBC_STATUS_BASE +
                ltc * GK208_LTC_CBC_STATUS_LTC_STRIDE +
                slice * GK208_LTC_CBC_STATUS_SLICE_STRIDE;
            int result = gr_wait_mask(region, status, UINT32_MAX, 0U,
                2000U, started, deadline);
            if (result != 0) return result;
        }
    }
    return 0;
}

static int gr_execute_operations(
        const uint8_t *storage, uint32_t pool_offset,
        const device_domain_gr_execution_header_t *header,
        const device_domain_region_info_t *region,
        const gr_prerequisite_plan_t *plan,
        uint64_t started, uint64_t deadline, uint32_t *context_size) {
    if (storage == NULL || header == NULL || region == NULL || plan == NULL ||
        context_size == NULL)
        return -22;
    const uint8_t *operations = storage + pool_offset +
        DEVICE_DOMAIN_GR_EXECUTION_HEADER_BYTES;
    uint32_t context_base = 0U;
    uint32_t context_starstar = 0U;
    uint32_t context_star = 0U;
    uint32_t context_remaining = 0U;
    for (uint32_t index = 0U; index < header->operation_count; ++index) {
        if (!gr_deadline_valid(started, deadline)) return -110;
        device_domain_gr_execution_op_t operation;
        memcpy(&operation,
               operations + index * DEVICE_DOMAIN_GR_EXECUTION_OP_BYTES,
               sizeof(operation));
        uint32_t value = 0U;
        int wait_result = 0;
        switch (operation.opcode) {
        case DEVICE_DOMAIN_GR_OP_WRITE32:
            if (!platform_ops.write_region(region, operation.address,
                    sizeof(uint32_t), operation.value))
                return -5;
            break;
        case DEVICE_DOMAIN_GR_OP_MASK32:
            if (!gr_read32(region, operation.address, &value) ||
                !platform_ops.write_region(region, operation.address,
                    sizeof(uint32_t),
                    (value & ~operation.mask) | operation.value))
                return -5;
            break;
        case DEVICE_DOMAIN_GR_OP_COPY_MASKED32:
            if (!gr_read32(region, operation.value, &value) ||
                !platform_ops.write_region(region, operation.address,
                    sizeof(uint32_t), value & operation.mask))
                return -5;
            break;
        case DEVICE_DOMAIN_GR_OP_VRAM_OFFSET32: {
            const uint32_t offset = operation.value == 1U
                ? plan->mmu_write_offset : plan->mmu_read_offset;
            if (operation.mask >= 32U ||
                (offset & ((1U << operation.mask) - 1U)) != 0U ||
                !platform_ops.write_region(region, operation.address,
                    sizeof(uint32_t), offset >> operation.mask))
                return -5;
            break;
        }
        case DEVICE_DOMAIN_GR_OP_WAIT_IDLE:
            wait_result = gr_wait_idle(
                region, &operation, started, deadline);
            if (wait_result != 0) return wait_result;
            break;
        case DEVICE_DOMAIN_GR_OP_CONTEXT_GROUP: {
            uint32_t first = 0U;
            uint32_t second = 0U;
            context_base = operation.address;
            context_starstar = operation.value;
            if (!platform_ops.write_region(region, context_base + 0x1C0U,
                    sizeof(uint32_t), 0x02000000U + context_starstar) ||
                !gr_read32(region, context_base + 0x1C4U, &first) ||
                !gr_read32(region, context_base + 0x1C4U, &second))
                return -5;
            context_star = first > second ? first : second;
            if ((context_star & 3U) != 0U || context_star > 0x00FFFFFCU ||
                !platform_ops.write_region(region, context_base + 0x1C0U,
                    sizeof(uint32_t), 0x01000000U + context_star))
                return -84;
            context_remaining = operation.mask;
            break;
        }
        case DEVICE_DOMAIN_GR_OP_CONTEXT_TRANSFER:
            if (context_remaining == 0U ||
                operation.address != context_base + 0x1C4U ||
                !platform_ops.write_region(region, operation.address,
                    sizeof(uint32_t), operation.value))
                return -5;
            --context_remaining;
            if (context_remaining == 0U &&
                (!platform_ops.write_region(region, context_base + 0x1C0U,
                    sizeof(uint32_t), 0x01000004U + context_starstar) ||
                 !platform_ops.write_region(region, context_base + 0x1C4U,
                    sizeof(uint32_t), context_star + 4U)))
                return -5;
            break;
        case DEVICE_DOMAIN_GR_OP_WAIT_MASK32:
            wait_result = gr_wait_mask(region, operation.address,
                operation.value, operation.value, operation.mask,
                started, deadline);
            if (wait_result != 0) return wait_result;
            break;
        case DEVICE_DOMAIN_GR_OP_READ32_NONZERO:
            if (!gr_read32(region, operation.address, &value)) return -5;
            if (value == 0U) return -84;
            *context_size = value;
            break;
        default:
            return -84;
        }
    }
    return context_remaining == 0U && *context_size != 0U ? 0 : -84;
}

static int gr_execution_rollback(device_slot_t *device, int status) {
    if (reset_gr_firmware_state(device)) {
        clear_gr_execution_state(device);
        return status;
    }
    (void)fence_slot(device);
    return -5;
}

int device_domain_gr_execute(
        int pid, uint32_t process_generation,
        const device_domain_gr_execution_request_t *request,
        device_domain_gr_execution_result_t *result) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        request == NULL || result == NULL ||
        request->version != DEVICE_DOMAIN_ABI_VERSION ||
        request->struct_size != sizeof(*request) || request->device == 0U ||
        request->region == 0U || request->dma == 0U ||
        request->policy_id == 0U || request->flags != 0U ||
        request->reserved[0] != 0U || request->reserved[1] != 0U ||
        request->reserved[2] != 0U)
        return -22;
    if (!begin_operation()) return -16;
    device_slot_t *device = owned_slot(
        pid, process_generation, request->device);
    resource_slot_t *region = owned_resource_locked(
        pid, process_generation, request->region,
        DEVICE_DOMAIN_RESOURCE_REGION);
    resource_slot_t *dma = owned_resource_locked(
        pid, process_generation, request->dma, DEVICE_DOMAIN_RESOURCE_DMA);
    dma_pool_slot_t *pool = dma_pool_for_resource(dma);
    if (device == NULL || region == NULL || dma == NULL) {
        end_operation();
        return -9;
    }
    const uint32_t device_index = (uint32_t)(device - devices);
    const device_domain_gr_prerequisite_policy_t *policy =
        &device->gr_prerequisite_policy;
    if (region->device_slot != device_index || dma->device_slot != device_index ||
        region->device_generation != device->generation ||
        dma->device_generation != device->generation ||
        device->state != DEVICE_DOMAIN_DMA_BOUND || pool == NULL ||
        pool->sealed == 0U || device->gr_prerequisite_active == 0U ||
        device->dma_vm_page_mode_active == 0U ||
        device->gr_firmware_active == 0U ||
        device->gr_execution_active != 0U ||
        request->policy_id != policy->policy_id ||
        region->region.region_index != policy->region_index ||
        (region->region.rights & DEVICE_DOMAIN_REGION_ACCESS_READ) == 0U) {
        end_operation();
        return -13;
    }

    const uint32_t pool_index = dma->platform_capability - 1U;
    const uint8_t *storage = dma_pool_storage[pool_index];
    gr_prerequisite_topology_t topology;
    gr_prerequisite_topology_t confirmed_topology;
    gr_prerequisite_plan_t plan;
    uint32_t image_crc = 0U;
    device_domain_gr_execution_header_t header;
    memcpy(&header, storage + policy->execution_pool_offset, sizeof(header));
    device_domain_region_info_t vram;
    bool preflight = gr_sample_topology(&region->region, &topology) &&
        gr_execution_image_valid(storage, pool->capacity, policy,
            &region->region, &topology, &image_crc) &&
        gr_build_prerequisite_plan(&region->region, policy, &plan) &&
        gr_sample_topology(&region->region, &confirmed_topology) &&
        memcmp(&topology, &confirmed_topology, sizeof(topology)) == 0 &&
        gr_plan_matches_device(&plan, device) &&
        image_crc == device->gr_prerequisite_image_crc &&
        gr_topology_crc32(&topology) ==
            device->gr_prerequisite_topology_crc &&
        platform_ops.describe_region(device->pci_location,
            policy->vram_region_index, &vram) &&
        (vram.flags & DEVICE_DOMAIN_REGION_MMIO) != 0U &&
        (vram.flags & DEVICE_DOMAIN_REGION_PIO) == 0U &&
        vram.base_high == 0U && vram.length_high == 0U &&
        vram.length_low == policy->vram_aperture_bytes;
    if (!preflight) {
        end_operation();
        return -84;
    }

    const uint64_t started = platform_ops.monotonic_ms();
    const uint64_t deadline = started >
            UINT64_MAX - DEVICE_DOMAIN_GR_EXECUTION_TIMEOUT_MS
        ? UINT64_MAX : started + DEVICE_DOMAIN_GR_EXECUTION_TIMEOUT_MS;
    device->gr_execution_active = 1U;
    int status = gr_zero_vram_buffer(&vram, plan.mmu_read_offset,
        policy->fault_buffer_bytes, started, deadline);
    if (status == 0)
        status = gr_zero_vram_buffer(&vram, plan.mmu_write_offset,
            policy->fault_buffer_bytes, started, deadline);
    if (status == 0)
        status = gr_initialize_ltc(
            &region->region, &plan, started, deadline);
    uint32_t context_size = 0U;
    if (status == 0)
        status = gr_execute_operations(storage, policy->execution_pool_offset,
            &header, &region->region, &plan, started, deadline,
            &context_size);
    if (status != 0) {
        status = gr_execution_rollback(device, status);
        end_operation();
        return status;
    }
    device->gr_execution_operation_count = header.operation_count;
    device->gr_execution_context_size = context_size;
    *result = (device_domain_gr_execution_result_t){
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(*result),
        .device = request->device,
        .policy_id = request->policy_id,
        .operation_count = header.operation_count,
        .context_size = context_size,
        .flags = DEVICE_DOMAIN_GR_EXECUTION_READY,
    };
    end_operation();
    return 0;
}

int device_domain_gr_context_memory(
        int pid, uint32_t process_generation,
        const device_domain_gr_context_memory_request_t *request,
        device_domain_gr_context_memory_result_t *result) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        request == NULL || result == NULL ||
        request->version != DEVICE_DOMAIN_ABI_VERSION ||
        request->struct_size != sizeof(*request) || request->device == 0U ||
        request->region == 0U || request->dma == 0U ||
        request->policy_id == 0U || request->flags != 0U ||
        request->reserved[0] != 0U || request->reserved[1] != 0U ||
        request->reserved[2] != 0U)
        return -22;
    if (!begin_operation()) return -16;
    device_slot_t *device = owned_slot(
        pid, process_generation, request->device);
    resource_slot_t *region = owned_resource_locked(
        pid, process_generation, request->region,
        DEVICE_DOMAIN_RESOURCE_REGION);
    resource_slot_t *dma = owned_resource_locked(
        pid, process_generation, request->dma, DEVICE_DOMAIN_RESOURCE_DMA);
    dma_pool_slot_t *pool = dma_pool_for_resource(dma);
    if (device == NULL || region == NULL || dma == NULL) {
        end_operation();
        return -9;
    }
    const uint32_t device_index = (uint32_t)(device - devices);
    const device_domain_gr_prerequisite_policy_t *policy =
        &device->gr_prerequisite_policy;
    if (region->device_slot != device_index || dma->device_slot != device_index ||
        region->device_generation != device->generation ||
        dma->device_generation != device->generation ||
        device->state != DEVICE_DOMAIN_DMA_BOUND || pool == NULL ||
        pool->sealed == 0U || device->gr_prerequisite_active == 0U ||
        device->dma_vm_page_mode_active == 0U ||
        device->gr_firmware_active == 0U ||
        device->gr_execution_active == 0U ||
        device->gr_execution_context_size == 0U ||
        device->gr_context_memory_active != 0U ||
        request->policy_id != policy->policy_id ||
        region->region.region_index != policy->region_index ||
        (region->region.rights & DEVICE_DOMAIN_REGION_ACCESS_READ) == 0U) {
        end_operation();
        return -13;
    }

    const uint8_t *storage =
        dma_pool_storage[dma->platform_capability - 1U];
    gr_prerequisite_topology_t topology;
    gr_prerequisite_topology_t confirmed_topology;
    gr_prerequisite_plan_t prerequisite;
    gr_context_memory_plan_t context;
    uint32_t image_crc = 0U;
    device_domain_gr_execution_header_t header;
    memcpy(&header, storage + policy->execution_pool_offset, sizeof(header));
    bool valid = gr_sample_topology(&region->region, &topology) &&
        gr_execution_image_valid(storage, pool->capacity, policy,
            &region->region, &topology, &image_crc) &&
        gr_build_prerequisite_plan(&region->region, policy, &prerequisite) &&
        gr_sample_topology(&region->region, &confirmed_topology) &&
        memcmp(&topology, &confirmed_topology, sizeof(topology)) == 0;
    const uint32_t confirmed_crc = valid ? gr_topology_crc32(&topology) : 0U;
    valid = valid && gr_plan_matches_device(&prerequisite, device) &&
        image_crc == device->gr_prerequisite_image_crc &&
        confirmed_crc != 0U &&
        confirmed_crc == device->gr_prerequisite_topology_crc &&
        header.operation_count == device->gr_execution_operation_count &&
        gr_build_context_memory_plan(&topology, &prerequisite, policy,
            device->gr_execution_context_size, &context);
    if (!valid) {
        end_operation();
        return -84;
    }
    device->gr_context_memory_active = 1U;
    device->gr_context_pagepool_offset = context.pagepool_offset;
    device->gr_context_bundle_offset = context.bundle_offset;
    device->gr_context_attrib_offset = context.attrib_offset;
    device->gr_context_attrib_bytes = context.attrib_bytes;
    device->gr_context_golden_offset = context.golden_offset;
    device->gr_context_golden_bytes = context.golden_bytes;
    device->gr_context_total_bytes = context.total_bytes;
    *result = (device_domain_gr_context_memory_result_t){
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(*result),
        .device = request->device,
        .policy_id = request->policy_id,
        .topology_crc32 = confirmed_crc,
        .tpc_total = topology.tpc_total,
        .pagepool_bytes = GK208_GR_PAGEPOOL_BYTES,
        .bundle_bytes = GK208_GR_BUNDLE_BYTES,
        .attrib_bytes = context.attrib_bytes,
        .context_size = device->gr_execution_context_size,
        .golden_bytes = context.golden_bytes,
        .total_bytes = context.total_bytes,
        .flags = DEVICE_DOMAIN_GR_CONTEXT_MEMORY_READY,
    };
    end_operation();
    return 0;
}

typedef struct {
    uint32_t instance_offset;
    uint32_t pgd_offset;
    uint32_t pgt_offset;
    uint32_t end_offset;
    uint32_t temporary_bytes;
    uint32_t gpu_address[4];
    uint32_t vram_offset[4];
    uint32_t mapped_bytes[4];
    uint32_t pte_first[4];
    uint32_t pte_count[4];
} gr_golden_memory_plan_t;

static bool gr_golden_build_memory_plan(
        const gr_context_memory_plan_t *context,
        const gr_prerequisite_plan_t *prerequisite,
        const device_domain_gr_prerequisite_policy_t *policy,
        gr_golden_memory_plan_t *plan) {
    if (context == NULL || prerequisite == NULL || policy == NULL ||
        plan == NULL || context->total_bytes == 0U)
        return false;
    uint64_t cursor = (uint64_t)context->pagepool_offset +
        context->total_bytes;
    if (!gr_align_up_u64(cursor, 4096U, &cursor) || cursor > UINT32_MAX)
        return false;
    const uint64_t instance = cursor;
    cursor += GK208_GR_TEMP_INSTANCE_BYTES;
    if (!gr_align_up_u64(cursor, 4096U, &cursor) || cursor > UINT32_MAX)
        return false;
    const uint64_t pgd = cursor;
    cursor += GK208_GR_TEMP_PGD_BYTES;
    if (!gr_align_up_u64(cursor, 4096U, &cursor) || cursor > UINT32_MAX)
        return false;
    const uint64_t pgt = cursor;
    cursor += GK208_GR_TEMP_PGT_BYTES;
    uint64_t usable_end = prerequisite->vram_bytes -
        GK208_VRAM_VBIOS_TAIL_BYTES;
    if (usable_end > policy->vram_aperture_bytes)
        usable_end = policy->vram_aperture_bytes;
    if (cursor > usable_end || cursor > UINT32_MAX ||
        cursor <= instance || cursor - instance > UINT32_MAX)
        return false;

    *plan = (gr_golden_memory_plan_t){
        .instance_offset = (uint32_t)instance,
        .pgd_offset = (uint32_t)pgd,
        .pgt_offset = (uint32_t)pgt,
        .end_offset = (uint32_t)cursor,
        .temporary_bytes = (uint32_t)(cursor - instance),
    };
    const uint32_t bytes[4] = {
        GK208_GR_PAGEPOOL_BYTES, GK208_GR_BUNDLE_BYTES,
        context->attrib_bytes, context->golden_bytes,
    };
    const uint32_t offsets[4] = {
        context->pagepool_offset, context->bundle_offset,
        context->attrib_offset, context->golden_offset,
    };
    uint64_t gpu = GK208_GR_GPU_BASE;
    const uint64_t table_end =
        (uint64_t)GK208_GR_GPU_TABLE_BASE + GK208_GR_GPU_TABLE_BYTES;
    for (uint32_t index = 0U; index < 4U; ++index) {
        uint64_t mapped = 0U;
        if (bytes[index] == 0U || (offsets[index] & 0xFFFU) != 0U ||
            !gr_align_up_u64(bytes[index], 4096U, &mapped) ||
            mapped == 0U || mapped > UINT32_MAX || gpu <
                GK208_GR_GPU_TABLE_BASE || gpu > table_end ||
            mapped > table_end - gpu)
            return false;
        const uint64_t first = (gpu - GK208_GR_GPU_TABLE_BASE) >>
            GK208_GR_GPU_PAGE_SHIFT;
        const uint64_t count = mapped >> GK208_GR_GPU_PAGE_SHIFT;
        if (first > UINT32_MAX || count == 0U || count > UINT32_MAX ||
            first + count > GK208_GR_TEMP_PGT_BYTES / 8U)
            return false;
        plan->gpu_address[index] = (uint32_t)gpu;
        plan->vram_offset[index] = offsets[index];
        plan->mapped_bytes[index] = (uint32_t)mapped;
        plan->pte_first[index] = (uint32_t)first;
        plan->pte_count[index] = (uint32_t)count;
        gpu += mapped;
    }
    return true;
}

static bool gr_golden_table_valid(void) {
    (void)reist_gk208_gr_mmio;
    (void)reist_gk208_gr_mmio_spans;
    (void)reist_gk208_gr_context_spans;
    uint32_t context_crc = UINT32_MAX;
    for (uint32_t index = 0U; index < REIST_GK208_GR_CONTEXT_TUPLE_COUNT;
         ++index) {
        const reist_nvidia_gk208_gr_tuple_t *tuple =
            &reist_gk208_gr_context[index];
        if (tuple->count == 0U || tuple->count > 255U ||
            (tuple->address & 3U) != 0U || tuple->pitch == 0U ||
            (tuple->pitch & 3U) != 0U ||
            (uint64_t)tuple->address +
                (uint64_t)(tuple->count - 1U) * tuple->pitch > 0x007FFFFCU)
            return false;
        context_crc = gr_crc32_word(context_crc, tuple->address);
        context_crc = gr_crc32_word(context_crc, tuple->count);
        context_crc = gr_crc32_word(context_crc, tuple->pitch);
        context_crc = gr_crc32_word(context_crc, tuple->value);
    }
    uint32_t icmd_crc = UINT32_MAX;
    for (uint32_t index = 0U; index < REIST_GK208_GR_ICMD_TUPLE_COUNT;
         ++index) {
        const reist_nvidia_gk208_gr_tuple_t *tuple =
            &reist_gk208_gr_icmd[index];
        if (tuple->count == 0U || tuple->count > 255U ||
            tuple->pitch != 1U || (uint64_t)tuple->address +
                tuple->count - 1U > 0x001FFFFFULL)
            return false;
        icmd_crc = gr_crc32_word(icmd_crc, tuple->address);
        icmd_crc = gr_crc32_word(icmd_crc, tuple->count);
        icmd_crc = gr_crc32_word(icmd_crc, tuple->pitch);
        icmd_crc = gr_crc32_word(icmd_crc, tuple->value);
    }
    uint32_t method_crc = UINT32_MAX;
    for (uint32_t index = 0U; index < REIST_GK208_GR_MTHD_TUPLE_COUNT;
         ++index) {
        const reist_nvidia_gk208_gr_method_tuple_t *method =
            &reist_gk208_gr_mthd[index];
        const reist_nvidia_gk208_gr_tuple_t *tuple = &method->tuple;
        if (tuple->count == 0U || tuple->count > 255U ||
            (tuple->address & 3U) != 0U || tuple->pitch == 0U ||
            (tuple->pitch & 3U) != 0U ||
            (uint64_t)tuple->address +
                (uint64_t)(tuple->count - 1U) * tuple->pitch > 0x0001FFFFU ||
            (method->class_id != 0x0000A197U &&
             method->class_id != 0x0000902DU))
            return false;
        method_crc = gr_crc32_word(method_crc, tuple->address);
        method_crc = gr_crc32_word(method_crc, tuple->count);
        method_crc = gr_crc32_word(method_crc, tuple->pitch);
        method_crc = gr_crc32_word(method_crc, tuple->value);
        method_crc = gr_crc32_word(method_crc, method->class_id);
    }
    return ~context_crc == REIST_GK208_GR_CONTEXT_CRC32 &&
        ~icmd_crc == REIST_GK208_GR_ICMD_CRC32 &&
        ~method_crc == REIST_GK208_GR_MTHD_CRC32;
}

static bool gr_write32(const device_domain_region_info_t *region,
                       uint32_t offset, uint32_t value) {
    return region != NULL && (offset & 3U) == 0U &&
        offset <= region->length_low &&
        sizeof(uint32_t) <= region->length_low - offset &&
        platform_ops.write_region(region, offset, sizeof(uint32_t), value);
}

static bool gr_mask32(const device_domain_region_info_t *region,
                      uint32_t offset, uint32_t mask, uint32_t value) {
    uint32_t current = 0U;
    return gr_read32(region, offset, &current) &&
        gr_write32(region, offset, (current & ~mask) | (value & mask));
}

static int gr_golden_wait_idle(const device_domain_region_info_t *region,
        uint64_t started, uint64_t deadline) {
    for (uint32_t attempt = 0U; attempt < 2000U; ++attempt) {
        uint32_t update = 0U;
        uint32_t busy = 0U;
        if (!gr_read32(region, 0x00400700U, &update) ||
            !gr_read32(region, 0x0040060CU, &busy))
            return -5;
        if ((busy & 1U) == 0U) return 0;
        if (!gr_wait_one_ms(started, deadline)) return -110;
    }
    return -110;
}

static int gr_golden_wait_fecs(const device_domain_region_info_t *region,
        uint32_t success, uint32_t error, uint64_t started,
        uint64_t deadline) {
    for (uint32_t attempt = 0U; attempt < 2000U; ++attempt) {
        uint32_t status = 0U;
        if (!gr_read32(region, GK208_GR_FECS_STATUS, &status)) return -5;
        if ((status & error) != 0U) return -5;
        if ((status & success) != 0U) return 0;
        if (!gr_wait_one_ms(started, deadline)) return -110;
    }
    return -110;
}

static int gr_golden_zero_window(
        const device_domain_region_info_t *vram,
        const gr_context_memory_plan_t *context,
        const gr_golden_memory_plan_t *golden,
        device_domain_region_info_t *window,
        uint64_t started, uint64_t deadline) {
    if (vram == NULL || context == NULL || golden == NULL || window == NULL ||
        golden->end_offset <= context->pagepool_offset ||
        vram->base_high != 0U || vram->length_high != 0U ||
        context->pagepool_offset > vram->length_low ||
        golden->end_offset > vram->length_low ||
        vram->base_low > UINT32_MAX - context->pagepool_offset)
        return -84;
    *window = *vram;
    window->base_low += context->pagepool_offset;
    window->length_low = golden->end_offset - context->pagepool_offset;
    window->length_high = 0U;
    if (!platform_ops.prepare_region(window)) return -5;
    for (uint32_t cursor = 0U; cursor < window->length_low; cursor += 4U) {
        if ((cursor & 0x3FFU) == 0U &&
            !gr_deadline_valid(started, deadline))
            return -110;
        if (!gr_write32(window, cursor, 0U)) return -5;
    }
    return 0;
}

static bool gr_golden_vram_write32(
        const device_domain_region_info_t *window, uint32_t window_base,
        uint32_t absolute_offset, uint32_t value) {
    return absolute_offset >= window_base &&
        gr_write32(window, absolute_offset - window_base, value);
}

static bool gr_golden_vram_read32(
        const device_domain_region_info_t *window, uint32_t window_base,
        uint32_t absolute_offset, uint32_t *value) {
    return absolute_offset >= window_base &&
        gr_read32(window, absolute_offset - window_base, value);
}

static bool gr_golden_vram_write64(
        const device_domain_region_info_t *window, uint32_t window_base,
        uint32_t absolute_offset, uint64_t value) {
    return gr_golden_vram_write32(window, window_base, absolute_offset,
            (uint32_t)value) &&
        gr_golden_vram_write32(window, window_base, absolute_offset + 4U,
            (uint32_t)(value >> 32U));
}

static int gr_golden_install_vm(
        const device_domain_region_info_t *mmio,
        const device_domain_region_info_t *window, uint32_t window_base,
        const gr_golden_memory_plan_t *plan,
        const gr_context_memory_plan_t *context,
        uint64_t started, uint64_t deadline) {
    if (mmio == NULL || window == NULL || plan == NULL || context == NULL)
        return -22;
    const uint32_t pgd_index = GK208_GR_GPU_TABLE_BASE >> 27U;
    if (!gr_golden_vram_write64(window, window_base,
            plan->instance_offset + GK208_GR_INSTANCE_PGD,
            (uint64_t)plan->pgd_offset) ||
        !gr_golden_vram_write32(window, window_base,
            plan->instance_offset + GK208_GR_INSTANCE_VM_LIMIT,
            0xFFFFFFFFU) ||
        !gr_golden_vram_write32(window, window_base,
            plan->instance_offset + GK208_GR_INSTANCE_VM_LIMIT + 4U,
            0x000000FFU) ||
        !gr_golden_vram_write64(window, window_base,
            plan->pgd_offset + pgd_index * 8U,
            ((uint64_t)plan->pgt_offset >> 8U) | 1ULL))
        return -5;
    for (uint32_t map = 0U; map < 4U; ++map) {
        for (uint32_t page = 0U; page < plan->pte_count[map]; ++page) {
            const uint32_t physical = plan->vram_offset[map] +
                (page << GK208_GR_GPU_PAGE_SHIFT);
            const uint64_t pte = ((uint64_t)physical >> 8U) | 1ULL;
            if (!gr_golden_vram_write64(window, window_base,
                    plan->pgt_offset +
                        (plan->pte_first[map] + page) * 8U, pte))
                return -5;
        }
    }
    if (!gr_golden_vram_write64(window, window_base,
            plan->instance_offset + GK208_GR_INSTANCE_CONTEXT,
            (uint64_t)(plan->gpu_address[3] +
                GK208_GR_GOLDEN_CB_RESERVED) | 4ULL) ||
        !gr_write32(mmio, 0x00100CB8U, (plan->pgd_offset >> 12U) << 4U) ||
        !gr_write32(mmio, 0x00100CBCU, 0x80000001U))
        return -5;
    return gr_wait_mask(mmio, GK208_FB_PAGE_CONFIG, 0x00008000U,
        0x00008000U, 2000U, started, deadline);
}

static int gr_golden_clear_vm(
        const device_domain_region_info_t *mmio,
        const device_domain_region_info_t *window, uint32_t window_base,
        const gr_golden_memory_plan_t *plan,
        uint64_t started, uint64_t deadline) {
    bool clean = gr_golden_vram_write64(window, window_base,
        plan->instance_offset + GK208_GR_INSTANCE_CONTEXT, 0ULL);
    for (uint32_t map = 0U; clean && map < 4U; ++map)
        for (uint32_t page = 0U; clean && page < plan->pte_count[map]; ++page)
            clean = gr_golden_vram_write64(window, window_base,
                plan->pgt_offset + (plan->pte_first[map] + page) * 8U, 0ULL);
    const uint32_t pgd_index = GK208_GR_GPU_TABLE_BASE >> 27U;
    clean = clean && gr_golden_vram_write64(window, window_base,
        plan->pgd_offset + pgd_index * 8U, 0ULL) &&
        gr_golden_vram_write64(window, window_base,
            plan->instance_offset + GK208_GR_INSTANCE_PGD, 0ULL);
    if (!clean || !gr_write32(mmio, 0x00100CB8U,
            (plan->pgd_offset >> 12U) << 4U) ||
        !gr_write32(mmio, 0x00100CBCU, 0x80000001U))
        return -5;
    return gr_wait_mask(mmio, GK208_FB_PAGE_CONFIG, 0x00008000U,
        0x00008000U, 2000U, started, deadline);
}

static int gr_golden_write_context_table(
        const device_domain_region_info_t *mmio,
        uint64_t started, uint64_t deadline) {
    for (uint32_t index = 0U; index < REIST_GK208_GR_CONTEXT_TUPLE_COUNT;
         ++index) {
        const reist_nvidia_gk208_gr_tuple_t *tuple =
            &reist_gk208_gr_context[index];
        uint32_t address = tuple->address;
        for (uint32_t item = 0U; item < tuple->count; ++item) {
            if (!gr_deadline_valid(started, deadline)) return -110;
            if (!gr_write32(mmio, address, tuple->value)) return -5;
            address += tuple->pitch;
        }
    }
    return 0;
}

static uint32_t gr_golden_screen_row(uint32_t total) {
    static const uint8_t primes[] = {
        3U, 5U, 7U, 11U, 13U, 17U, 19U, 23U, 29U, 31U,
        37U, 41U, 43U, 47U, 53U, 59U, 61U,
    };
    switch (total) {
    case 15U: return 6U;
    case 14U: return 5U;
    case 13U: return 2U;
    case 11U: return 7U;
    case 10U: return 6U;
    case 7U:
    case 5U: return 1U;
    case 3U: return 2U;
    case 2U:
    case 1U: return 1U;
    default:
        for (uint32_t index = 0U; index < sizeof(primes); ++index)
            if (total % primes[index] != 0U) return primes[index];
        return 3U;
    }
}

static bool gr_golden_tile_map(const gr_prerequisite_topology_t *topology,
        uint32_t tile[GK208_GR_MAX_TOTAL_TPCS]) {
    int32_t fraction[DEVICE_DOMAIN_GR_MAX_GPCS];
    int32_t error[DEVICE_DOMAIN_GR_MAX_GPCS];
    uint32_t order[DEVICE_DOMAIN_GR_MAX_GPCS];
    for (uint32_t gpc = 0U; gpc < topology->gpc_count; ++gpc)
        order[gpc] = gpc;
    for (uint32_t pass = 0U; pass < topology->gpc_count; ++pass)
        for (uint32_t gpc = 0U; gpc + 1U < topology->gpc_count; ++gpc)
            if (topology->tpc_count[order[gpc + 1U]] >
                topology->tpc_count[order[gpc]]) {
                const uint32_t swap = order[gpc];
                order[gpc] = order[gpc + 1U];
                order[gpc + 1U] = swap;
            }
    uint32_t multiplier = topology->gpc_count * topology->tpc_max;
    multiplier = (multiplier & 1U) != 0U ? 2U : 1U;
    const int32_t denominator = (int32_t)(
        topology->gpc_count * topology->tpc_max * multiplier);
    for (uint32_t gpc = 0U; gpc < topology->gpc_count; ++gpc) {
        fraction[gpc] = (int32_t)(topology->tpc_count[order[gpc]] *
            topology->gpc_count * multiplier);
        error[gpc] = fraction[gpc] +
            (int32_t)(gpc * topology->tpc_max * multiplier) - denominator / 2;
    }
    uint32_t count = 0U;
    for (uint32_t cycle = 0U;
         cycle < GK208_GR_MAX_TOTAL_TPCS * DEVICE_DOMAIN_GR_MAX_GPCS * 2U &&
             count < topology->tpc_total; ++cycle)
        for (uint32_t gpc = 0U;
             gpc < topology->gpc_count && count < topology->tpc_total; ++gpc)
            if (error[gpc] * 2 >= denominator) {
                tile[count++] = order[gpc];
                error[gpc] += fraction[gpc] - denominator;
            } else {
                error[gpc] += fraction[gpc];
            }
    return count == topology->tpc_total;
}

static int gr_golden_floorsweep(
        const device_domain_region_info_t *mmio,
        const gr_prerequisite_topology_t *topology,
        uint64_t started, uint64_t deadline) {
    uint32_t sm = 0U;
    for (uint32_t tpc = 0U; tpc < topology->tpc_max; ++tpc) {
        for (uint32_t gpc = 0U; gpc < topology->gpc_count; ++gpc) {
            if (tpc >= topology->tpc_count[gpc]) continue;
            const uint32_t gpc_base = GK208_GR_GPC_UNIT_BASE +
                gpc * GK208_GR_GPC_UNIT_STRIDE;
            const uint32_t tpc_base = 0x00504000U +
                gpc * GK208_GR_GPC_UNIT_STRIDE + tpc * 0x800U;
            if (!gr_write32(mmio, tpc_base + 0x698U, sm) ||
                !gr_write32(mmio, tpc_base + 0x4E8U, sm) ||
                !gr_write32(mmio, gpc_base + 0x0C10U + tpc * 4U, sm) ||
                !gr_write32(mmio, tpc_base + 0x088U, sm) ||
                !gr_write32(mmio, gpc_base + 0x0C08U,
                    topology->tpc_count[gpc]) ||
                !gr_write32(mmio, gpc_base + 0x0C8CU,
                    topology->tpc_count[gpc]))
                return -5;
            ++sm;
        }
    }
    for (uint32_t group = 0U; group < 4U; ++group) {
        uint32_t packed = 0U;
        for (uint32_t item = 0U; item < 8U; ++item) {
            const uint32_t gpc = group * 8U + item;
            if (gpc < topology->gpc_count)
                packed |= topology->tpc_count[gpc] << (item * 4U);
        }
        if (!gr_write32(mmio, 0x00405070U + group * 4U, packed) ||
            !gr_write32(mmio, 0x00406028U + group * 4U, packed))
            return -5;
    }
    uint32_t tile[GK208_GR_MAX_TOTAL_TPCS] = {0U};
    if (!gr_golden_tile_map(topology, tile) || topology->tpc_total > 16U)
        return -84;
    uint32_t data[6] = {0U};
    for (uint32_t index = 0U; index < 32U; ++index)
        data[index / 6U] |= (tile[index] & 7U) << ((index % 6U) * 5U);
    uint32_t shift = 0U;
    uint32_t ntpcv = topology->tpc_total;
    for (; (ntpcv & 16U) == 0U && shift < 4U; ++shift)
        ntpcv <<= 1U;
    if ((ntpcv & 16U) == 0U) return -84;
    uint32_t data2_0 = (ntpcv << 16U) | (shift << 21U) |
        (((1U << 5U) % ntpcv) << 24U);
    uint32_t data2_1 = 0U;
    for (uint32_t index = 1U; index < 7U; ++index)
        data2_1 |= ((1U << (index + 5U)) % ntpcv) <<
            ((index - 1U) * 5U);
    const uint32_t row = gr_golden_screen_row(topology->tpc_total);
    if (!gr_write32(mmio, 0x00418BB8U,
            (topology->tpc_total << 8U) | row) ||
        !gr_write32(mmio, 0x0041BFD0U,
            (topology->tpc_total << 8U) | row | data2_0) ||
        !gr_write32(mmio, 0x0041BFE4U, data2_1) ||
        !gr_write32(mmio, 0x004078BCU,
            (topology->tpc_total << 8U) | row))
        return -5;
    for (uint32_t index = 0U; index < 6U; ++index)
        if (!gr_write32(mmio, 0x00418B08U + index * 4U, data[index]) ||
            !gr_write32(mmio, 0x0041BF00U + index * 4U, data[index]) ||
            !gr_write32(mmio, 0x0040780CU + index * 4U, data[index]))
            return -5;

    for (uint32_t entry = 0U; entry < 32U; ++entry) {
        uint32_t atarget = topology->tpc_total * entry / 32U;
        if (atarget == 0U) atarget = 1U;
        uint32_t btarget = topology->tpc_total - atarget;
        bool alpha = atarget < btarget;
        uint32_t amask[8] = {0U};
        uint32_t bmask[8] = {0U};
        for (uint32_t gpc = 0U; gpc < topology->gpc_count; ++gpc) {
            const uint32_t tpcs = topology->tpc_count[gpc];
            uint32_t abits = alpha ? (atarget != 0U ? tpcs : 0U) :
                tpcs - (btarget != 0U ? tpcs : 0U);
            uint32_t bbits = tpcs - abits;
            uint32_t pmask = topology->ppc_tpc_mask[gpc];
            for (uint32_t remaining = tpcs; remaining > abits; --remaining)
                pmask &= pmask - 1U;
            amask[gpc / 4U] |= pmask << ((gpc % 4U) * 8U);
            pmask ^= topology->ppc_tpc_mask[gpc];
            bmask[gpc / 4U] |= pmask << ((gpc % 4U) * 8U);
            atarget -= abits < atarget ? abits : atarget;
            btarget -= bbits < btarget ? bbits : btarget;
            if (abits != 0U || bbits != 0U) alpha = !alpha;
        }
        for (uint32_t group = 0U;
             group < (topology->gpc_count + 3U) / 4U; ++group)
            if (!gr_write32(mmio,
                    0x00406800U + entry * 0x20U + group * 4U,
                    amask[group]) ||
                !gr_write32(mmio,
                    0x00406C00U + entry * 0x20U + group * 4U,
                    bmask[group]))
                return -5;
    }
    for (uint32_t index = 0U; index < 8U; ++index)
        if (!gr_write32(mmio, 0x004064D0U + index * 4U, 0U)) return -5;
    if (!gr_write32(mmio, 0x00405B00U,
            (topology->tpc_total << 8U) | topology->gpc_count) ||
        !gr_mask32(mmio, 0x00419F78U, 0x00000008U, 0U))
        return -5;
    return gr_deadline_valid(started, deadline) ? 0 : -110;
}

static int gr_golden_patch_buffers(
        const device_domain_region_info_t *mmio,
        const gr_prerequisite_topology_t *topology,
        const gr_golden_memory_plan_t *plan) {
    const uint32_t pagepool = plan->gpu_address[0];
    const uint32_t bundle = plan->gpu_address[1];
    const uint32_t attrib = plan->gpu_address[2];
    if (!gr_write32(mmio, 0x0040800CU, pagepool >> 8U) ||
        !gr_write32(mmio, 0x00408010U, 0x80000000U) ||
        !gr_write32(mmio, 0x00419004U, pagepool >> 8U) ||
        !gr_write32(mmio, 0x00419008U, 0U) ||
        !gr_write32(mmio, 0x004064CCU, 0x80000000U) ||
        !gr_write32(mmio, 0x00408004U, bundle >> 8U) ||
        !gr_write32(mmio, 0x00408008U,
            0x80000000U | (GK208_GR_BUNDLE_BYTES >> 8U)) ||
        !gr_write32(mmio, 0x00418808U, bundle >> 8U) ||
        !gr_write32(mmio, 0x0041880CU,
            0x80000000U | (GK208_GR_BUNDLE_BYTES >> 8U)) ||
        !gr_write32(mmio, 0x004064C8U, (0xC2U << 16U) | 0x200U) ||
        !gr_write32(mmio, 0x00418810U, 0x80000000U | (attrib >> 12U)) ||
        !gr_write32(mmio, 0x00419848U, 0x10000000U | (attrib >> 12U)) ||
        !gr_write32(mmio, 0x00405830U, (0x218U << 16U) | 0x648U) ||
        !gr_write32(mmio, 0x004064C4U,
            ((0x648U / 4U) << 16U) | 0xFFFFU))
        return -5;
    uint32_t beta_offset = 0U;
    uint32_t alpha_offset = 0x324U * topology->tpc_total;
    for (uint32_t gpc = 0U; gpc < topology->gpc_count; ++gpc) {
        const uint32_t ppc = 0x00503000U +
            gpc * GK208_GR_GPC_UNIT_STRIDE;
        const uint32_t tpcs = topology->tpc_count[gpc];
        if (!gr_write32(mmio, ppc + 0xC0U,
                (1U << 28U) | ((0x218U * tpcs) << 16U) | beta_offset) ||
            !gr_write32(mmio, ppc + 0xE4U,
                ((0x648U * tpcs) << 16U) | alpha_offset))
            return -5;
        beta_offset += 0x324U * tpcs;
        alpha_offset += 0x7FFU * tpcs;
    }
    uint32_t ltc = 0U;
    if (!gr_read32(mmio, 0x0017E91CU, &ltc) ||
        !gr_write32(mmio, 0x0017E91CU, ltc) ||
        !gr_read32(mmio, 0x0017E920U, &ltc) ||
        !gr_write32(mmio, 0x0017E920U, ltc) ||
        !gr_mask32(mmio, 0x00418C6CU, 0x00000001U, 0x00000001U) ||
        !gr_mask32(mmio, 0x0041980CU, 0x00000010U, 0x00000010U) ||
        !gr_mask32(mmio, 0x0041BE08U, 0x00000004U, 0x00000004U) ||
        !gr_mask32(mmio, 0x004064C0U, 0x80000000U, 0x80000000U) ||
        !gr_mask32(mmio, 0x00405800U, 0x08000000U, 0x08000000U) ||
        !gr_mask32(mmio, 0x00419C00U, 0x00000008U, 0x00000008U))
        return -5;
    return 0;
}

static int gr_golden_icmd(const device_domain_region_info_t *mmio,
        uint64_t started, uint64_t deadline) {
    if (!gr_write32(mmio, 0x00400208U, 0x80000000U)) return -5;
    uint32_t previous = 0U;
    bool first = true;
    for (uint32_t index = 0U; index < REIST_GK208_GR_ICMD_TUPLE_COUNT;
         ++index) {
        const reist_nvidia_gk208_gr_tuple_t *tuple =
            &reist_gk208_gr_icmd[index];
        if (first || previous != tuple->value) {
            if (!gr_write32(mmio, 0x00400204U, tuple->value)) return -5;
            previous = tuple->value;
            first = false;
        }
        uint32_t address = tuple->address;
        for (uint32_t item = 0U; item < tuple->count; ++item) {
            if (!gr_write32(mmio, 0x00400200U, address)) return -5;
            int status = 0;
            if ((address & 0xFFFFU) == 0xE100U)
                status = gr_golden_wait_idle(mmio, started, deadline);
            if (status == 0)
                status = gr_wait_mask(mmio, 0x00400700U, 4U, 0U,
                    2000U, started, deadline);
            if (status != 0) return status;
            address += tuple->pitch;
        }
    }
    return gr_write32(mmio, 0x00400208U, 0U) ? 0 : -5;
}

static int gr_golden_methods(const device_domain_region_info_t *mmio,
        uint64_t started, uint64_t deadline) {
    uint32_t previous = 0U;
    bool first = true;
    for (uint32_t index = 0U; index < REIST_GK208_GR_MTHD_TUPLE_COUNT;
         ++index) {
        const reist_nvidia_gk208_gr_method_tuple_t *method =
            &reist_gk208_gr_mthd[index];
        const reist_nvidia_gk208_gr_tuple_t *tuple = &method->tuple;
        if (first || previous != tuple->value) {
            if (!gr_write32(mmio, 0x0040448CU, tuple->value)) return -5;
            previous = tuple->value;
            first = false;
        }
        uint32_t address = tuple->address;
        for (uint32_t item = 0U; item < tuple->count; ++item) {
            if (!gr_deadline_valid(started, deadline)) return -110;
            if (!gr_write32(mmio, 0x00404488U,
                    0x80000000U | method->class_id | (address << 14U)))
                return -5;
            address += tuple->pitch;
        }
    }
    return 0;
}

static int gr_golden_context_crc(
        const device_domain_region_info_t *window, uint32_t window_base,
        uint32_t context_offset, uint32_t context_size,
        uint64_t started, uint64_t deadline, uint32_t *crc_out) {
    if (window == NULL || crc_out == NULL || context_size == 0U ||
        (context_size & 3U) != 0U)
        return -84;
    uint32_t crc = UINT32_MAX;
    for (uint32_t cursor = 0U; cursor < context_size; cursor += 4U) {
        uint32_t word = 0U;
        if ((cursor & 0x3FFU) == 0U &&
            !gr_deadline_valid(started, deadline))
            return -110;
        if (!gr_golden_vram_read32(window, window_base,
                context_offset + cursor, &word))
            return -5;
        crc = gr_crc32_word(crc, word);
    }
    *crc_out = ~crc;
    return *crc_out != 0U ? 0 : -84;
}

static int gr_golden_execute_transaction(
        const device_domain_region_info_t *mmio,
        const device_domain_region_info_t *vram,
        const gr_prerequisite_topology_t *topology,
        const gr_context_memory_plan_t *context,
        const gr_golden_memory_plan_t *plan,
        uint32_t context_size, uint64_t started, uint64_t deadline,
        uint32_t *crc_out) {
    device_domain_region_info_t window = {0};
    int status = gr_golden_zero_window(
        vram, context, plan, &window, started, deadline);
    const bool window_ready = status == 0;
    const uint32_t base = context->pagepool_offset;
    if (status == 0)
        status = gr_golden_install_vm(mmio, &window, base, plan, context,
            started, deadline);
    if (status == 0 &&
        !gr_write32(mmio, GK208_GR_FE_POWER, 0x00000012U)) status = -5;
    if (status == 0)
        status = gr_wait_mask(mmio, GK208_GR_FE_POWER, 0x10U, 0U, 2000U,
            started, deadline);
    if (status == 0 &&
        (!gr_write32(mmio, GK208_GR_FECS_RESET, 0x00000070U) ||
         !gr_mask32(mmio, GK208_GR_FECS_RESET,
             0x00000700U, 0x00000700U) ||
         !gr_write32(mmio, GK208_GR_FE_POWER, 0x00000010U)))
        status = -5;
    if (status == 0)
        status = gr_wait_mask(mmio, GK208_GR_FE_POWER, 0x10U, 0U, 2000U,
            started, deadline);
    if (status == 0 && !gr_write32(mmio, 0x0040802CU, 1U)) status = -5;
    const uint32_t instance = 0x80000000U |
        (plan->instance_offset >> 12U);
    if (status == 0 &&
        (!gr_mask32(mmio, GK208_GR_FECS_STATUS, 0x30U, 0U) ||
         !gr_write32(mmio, GK208_GR_FECS_DATA, instance) ||
         !gr_write32(mmio, GK208_GR_FECS_METHOD, 3U)))
        status = -5;
    if (status == 0)
        status = gr_golden_wait_fecs(
            mmio, 0x10U, 0x20U, started, deadline);
    if (status == 0 &&
        (!gr_golden_vram_write32(&window, base,
             context->golden_offset + 0x1CU, 1U) ||
         !gr_golden_vram_write32(&window, base,
             context->golden_offset + 0x20U, 0U) ||
         !gr_golden_vram_write32(&window, base,
             context->golden_offset + 0x28U, 0U) ||
         !gr_golden_vram_write32(&window, base,
             context->golden_offset + 0x2CU, 0U)))
        status = -5;
    if (status == 0 && !gr_write32(mmio, 0x00000260U, 0U)) status = -5;
    if (status == 0)
        status = gr_golden_write_context_table(mmio, started, deadline);
    if (status == 0)
        status = gr_golden_wait_idle(mmio, started, deadline);
    uint32_t idle_timeout = 0U;
    if (status == 0 &&
        (!gr_read32(mmio, 0x00404154U, &idle_timeout) ||
         !gr_write32(mmio, 0x00404154U, 0U)))
        status = -5;
    if (status == 0)
        status = gr_golden_patch_buffers(mmio, topology, plan);
    if (status == 0)
        status = gr_golden_floorsweep(mmio, topology, started, deadline);
    if (status == 0)
        status = gr_golden_wait_idle(mmio, started, deadline);
    if (status == 0)
        status = gr_golden_icmd(mmio, started, deadline);
    if (status == 0 && !gr_write32(mmio, 0x00404154U, idle_timeout))
        status = -5;
    if (status == 0)
        status = gr_golden_methods(mmio, started, deadline);
    if (status == 0 && !gr_write32(mmio, 0x00000260U, 1U)) status = -5;
    if (status == 0)
        status = gr_golden_wait_idle(mmio, started, deadline);
    if (status == 0 &&
        (!gr_mask32(mmio, GK208_GR_FECS_STATUS, 3U, 0U) ||
         !gr_write32(mmio, GK208_GR_FECS_DATA, instance) ||
         !gr_write32(mmio, GK208_GR_FECS_METHOD, 9U)))
        status = -5;
    if (status == 0)
        status = gr_golden_wait_fecs(mmio, 1U, 2U, started, deadline);
    if (status == 0 &&
        !gr_mask32(mmio, GK208_GR_FECS_CURRENT, 0x80000000U, 0U))
        status = -5;
    if (status == 0)
        status = gr_golden_context_crc(&window, base,
            context->golden_offset + GK208_GR_GOLDEN_CB_RESERVED,
            context_size, started, deadline, crc_out);
    const int clear_status = window_ready ? gr_golden_clear_vm(
        mmio, &window, base, plan, started, deadline) : 0;
    return status != 0 ? status : clear_status;
}

int device_domain_gr_golden_context(
        int pid, uint32_t process_generation,
        const device_domain_gr_golden_context_request_t *request,
        device_domain_gr_golden_context_result_t *result) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        request == NULL || result == NULL ||
        request->version != DEVICE_DOMAIN_ABI_VERSION ||
        request->struct_size != sizeof(*request) || request->device == 0U ||
        request->region == 0U || request->dma == 0U ||
        request->policy_id == 0U || request->flags != 0U ||
        request->reserved[0] != 0U || request->reserved[1] != 0U ||
        request->reserved[2] != 0U)
        return -22;
    if (!begin_operation()) return -16;
    device_slot_t *device = owned_slot(
        pid, process_generation, request->device);
    resource_slot_t *region = owned_resource_locked(
        pid, process_generation, request->region,
        DEVICE_DOMAIN_RESOURCE_REGION);
    resource_slot_t *dma = owned_resource_locked(
        pid, process_generation, request->dma, DEVICE_DOMAIN_RESOURCE_DMA);
    dma_pool_slot_t *pool = dma_pool_for_resource(dma);
    if (device == NULL || region == NULL || dma == NULL) {
        end_operation();
        return -9;
    }
    const uint32_t device_index = (uint32_t)(device - devices);
    const device_domain_gr_prerequisite_policy_t *policy =
        &device->gr_prerequisite_policy;
    if (region->device_slot != device_index || dma->device_slot != device_index ||
        region->device_generation != device->generation ||
        dma->device_generation != device->generation ||
        device->state != DEVICE_DOMAIN_DMA_BOUND || pool == NULL ||
        pool->sealed == 0U || device->gr_prerequisite_active == 0U ||
        device->dma_vm_page_mode_active == 0U ||
        device->gr_firmware_active == 0U ||
        device->gr_execution_active == 0U ||
        device->gr_context_memory_active == 0U ||
        device->gr_golden_context_active != 0U ||
        device->gr_execution_context_size == 0U ||
        (device->gr_execution_context_size & 3U) != 0U ||
        request->policy_id != policy->policy_id ||
        region->region.region_index != policy->region_index ||
        (region->region.rights & DEVICE_DOMAIN_REGION_ACCESS_READ) == 0U) {
        end_operation();
        return -13;
    }
    const uint8_t *storage =
        dma_pool_storage[dma->platform_capability - 1U];
    gr_prerequisite_topology_t topology;
    gr_prerequisite_topology_t confirmed;
    gr_prerequisite_plan_t prerequisite;
    gr_context_memory_plan_t context;
    gr_golden_memory_plan_t golden;
    device_domain_gr_execution_header_t header;
    uint32_t image_crc = 0U;
    memcpy(&header, storage + policy->execution_pool_offset, sizeof(header));
    device_domain_region_info_t vram;
    bool valid = gr_sample_topology(&region->region, &topology) &&
        gr_execution_image_valid(storage, pool->capacity, policy,
            &region->region, &topology, &image_crc) &&
        gr_build_prerequisite_plan(&region->region, policy, &prerequisite) &&
        gr_sample_topology(&region->region, &confirmed) &&
        memcmp(&topology, &confirmed, sizeof(topology)) == 0 &&
        gr_plan_matches_device(&prerequisite, device) &&
        image_crc == device->gr_prerequisite_image_crc &&
        gr_topology_crc32(&topology) ==
            device->gr_prerequisite_topology_crc &&
        header.operation_count == device->gr_execution_operation_count &&
        gr_build_context_memory_plan(&topology, &prerequisite, policy,
            device->gr_execution_context_size, &context) &&
        context.pagepool_offset == device->gr_context_pagepool_offset &&
        context.bundle_offset == device->gr_context_bundle_offset &&
        context.attrib_offset == device->gr_context_attrib_offset &&
        context.attrib_bytes == device->gr_context_attrib_bytes &&
        context.golden_offset == device->gr_context_golden_offset &&
        context.golden_bytes == device->gr_context_golden_bytes &&
        context.total_bytes == device->gr_context_total_bytes &&
        gr_golden_build_memory_plan(
            &context, &prerequisite, policy, &golden) &&
        gr_golden_table_valid() &&
        platform_ops.describe_region(device->pci_location,
            policy->vram_region_index, &vram) &&
        (vram.flags & DEVICE_DOMAIN_REGION_MMIO) != 0U &&
        (vram.flags & DEVICE_DOMAIN_REGION_PIO) == 0U &&
        vram.base_high == 0U && vram.length_high == 0U &&
        vram.length_low == policy->vram_aperture_bytes;
    if (!valid) {
        end_operation();
        return -84;
    }
    const uint64_t started = platform_ops.monotonic_ms();
    const uint64_t deadline = started >
            UINT64_MAX - DEVICE_DOMAIN_GR_EXECUTION_TIMEOUT_MS
        ? UINT64_MAX : started + DEVICE_DOMAIN_GR_EXECUTION_TIMEOUT_MS;
    uint32_t context_crc = 0U;
    int status = gr_golden_execute_transaction(
        &region->region, &vram, &topology, &context, &golden,
        device->gr_execution_context_size, started, deadline, &context_crc);
    if (status != 0) {
        status = gr_execution_rollback(device, status);
        end_operation();
        return status;
    }
    device->gr_golden_context_active = 1U;
    device->gr_golden_context_crc32 = context_crc;
    device->gr_golden_context_retained_bytes = context.golden_bytes;
    *result = (device_domain_gr_golden_context_result_t){
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(*result),
        .device = request->device,
        .policy_id = request->policy_id,
        .topology_crc32 = device->gr_prerequisite_topology_crc,
        .context_tuple_count = REIST_GK208_GR_CONTEXT_TUPLE_COUNT,
        .icmd_tuple_count = REIST_GK208_GR_ICMD_TUPLE_COUNT,
        .method_tuple_count = REIST_GK208_GR_MTHD_TUPLE_COUNT,
        .context_size = device->gr_execution_context_size,
        .retained_bytes = context.golden_bytes,
        .context_crc32 = context_crc,
        .temporary_bytes = golden.temporary_bytes,
        .flags = DEVICE_DOMAIN_GR_GOLDEN_CONTEXT_READY |
            DEVICE_DOMAIN_GR_GOLDEN_CONTEXT_OPAQUE_VRAM,
    };
    end_operation();
    return 0;
}

typedef struct {
    uint32_t instance_offset;
    uint32_t pgd_offset;
    uint32_t pgt_offset;
    uint32_t userd_offset;
    uint32_t runlist_offset;
    uint32_t end_offset;
    uint32_t surface_gpu_address;
    uint32_t surface_pte_first;
    uint32_t surface_pte_count;
    uint32_t surface_page_offset;
} gr_channel_memory_plan_t;

static bool gr_channel_build_memory_plan(
        const gr_golden_memory_plan_t *golden,
        const device_domain_gr_prerequisite_policy_t *prerequisite,
        const device_domain_gr_channel_policy_t *policy,
        gr_channel_memory_plan_t *plan) {
    if (golden == NULL || prerequisite == NULL || policy == NULL ||
        plan == NULL || golden->end_offset == 0U) return false;
    uint64_t cursor = golden->end_offset;
    const uint64_t userd = cursor;
    cursor += GK208_CHANNEL_USERD_BYTES;
    if (!gr_align_up_u64(cursor, 4096U, &cursor) || cursor > UINT32_MAX)
        return false;
    const uint64_t runlist = cursor;
    cursor += GK208_CHANNEL_RUNLIST_BYTES;
    uint64_t usable_end = prerequisite->vram_aperture_bytes;
    if (usable_end > GK208_VRAM_VBIOS_TAIL_BYTES)
        usable_end -= GK208_VRAM_VBIOS_TAIL_BYTES;
    else return false;
    const uint32_t page_offset = prerequisite->scanout_offset & 0xFFFU;
    uint64_t mapped = (uint64_t)page_offset + prerequisite->scanout_bytes;
    if (!gr_align_up_u64(mapped, 4096U, &mapped) || mapped == 0U ||
        cursor > usable_end || mapped > UINT32_MAX) return false;
    const uint64_t surface = (uint64_t)GK208_CHANNEL_SURFACE_GPU + page_offset;
    const uint64_t first = (GK208_CHANNEL_SURFACE_GPU -
        GK208_GR_GPU_TABLE_BASE) >> GK208_GR_GPU_PAGE_SHIFT;
    const uint64_t count = mapped >> GK208_GR_GPU_PAGE_SHIFT;
    if (surface > UINT32_MAX || (surface & 0xFFU) != 0U ||
        first + count > GK208_GR_TEMP_PGT_BYTES / 8U ||
        (uint64_t)policy->pitch * policy->height !=
            prerequisite->scanout_bytes)
        return false;
    *plan = (gr_channel_memory_plan_t){
        .instance_offset = golden->instance_offset,
        .pgd_offset = golden->pgd_offset,
        .pgt_offset = golden->pgt_offset,
        .userd_offset = (uint32_t)userd,
        .runlist_offset = (uint32_t)runlist,
        .end_offset = (uint32_t)cursor,
        .surface_gpu_address = (uint32_t)surface,
        .surface_pte_first = (uint32_t)first,
        .surface_pte_count = (uint32_t)count,
        .surface_page_offset = prerequisite->scanout_offset - page_offset,
    };
    return true;
}

static bool gr_channel_vram_write64(
        const device_domain_region_info_t *window, uint32_t base,
        uint32_t offset, uint64_t value) {
    return gr_golden_vram_write64(window, base, offset, value);
}

static bool gr_channel_zero_private(
        const device_domain_region_info_t *window, uint32_t base,
        const gr_channel_memory_plan_t *plan,
        uint64_t started, uint64_t deadline) {
    if (window == NULL || plan == NULL || plan->instance_offset < base ||
        plan->end_offset <= plan->instance_offset) return false;
    for (uint32_t offset = plan->instance_offset;
         offset < plan->end_offset; offset += sizeof(uint32_t)) {
        if ((offset & 0x3FFU) == 0U &&
            !gr_deadline_valid(started, deadline)) return false;
        if (!gr_golden_vram_write32(window, base, offset, 0U)) return false;
    }
    return true;
}

static bool gr_channel_map_pte(
        const device_domain_region_info_t *window, uint32_t base,
        uint32_t pgt_offset, uint32_t pte, uint64_t value) {
    return pte < GK208_GR_TEMP_PGT_BYTES / 8U &&
        gr_channel_vram_write64(window, base,
            pgt_offset + pte * 8U, value);
}

static bool gr_channel_install_vm(
        const device_domain_region_info_t *mmio,
        const device_domain_region_info_t *window, uint32_t base,
        const gr_golden_memory_plan_t *golden,
        const gr_context_memory_plan_t *context,
        const gr_channel_memory_plan_t *channel,
        const device_domain_gr_prerequisite_policy_t *prerequisite,
        uint32_t pool_index, uint64_t started, uint64_t deadline) {
    const uint32_t pgd_index = GK208_GR_GPU_TABLE_BASE >> 27U;
    if (!gr_channel_vram_write64(window, base,
            channel->instance_offset + GK208_GR_INSTANCE_PGD,
            channel->pgd_offset) ||
        !gr_golden_vram_write32(window, base,
            channel->instance_offset + GK208_GR_INSTANCE_VM_LIMIT,
            0xFFFFFFFFU) ||
        !gr_golden_vram_write32(window, base,
            channel->instance_offset + GK208_GR_INSTANCE_VM_LIMIT + 4U,
            0x000000FFU) ||
        !gr_channel_vram_write64(window, base,
            channel->pgd_offset + pgd_index * 8U,
            ((uint64_t)channel->pgt_offset >> 8U) | 1ULL)) return false;
    const uint64_t system_gpu[3] = {
        GK208_CHANNEL_PUSH_GPU, GK208_CHANNEL_FENCE_GPU,
        GK208_CHANNEL_GPFIFO_GPU,
    };
    const uint32_t system_pool[3] = {
        GK208_CHANNEL_PUSH_POOL_OFFSET, GK208_CHANNEL_FENCE_POOL_OFFSET,
        GK208_CHANNEL_GPFIFO_POOL_OFFSET,
    };
    for (uint32_t index = 0U; index < 3U; ++index) {
        const uint64_t physical = dma_pool_physical_address(
            pool_index, system_pool[index]);
        const uint32_t pte = (uint32_t)((system_gpu[index] -
            GK208_GR_GPU_TABLE_BASE) >> GK208_GR_GPU_PAGE_SHIFT);
        uint64_t value = (physical >> 8U) | GK208_VM_PTE_NCOH;
        if (index != 1U) value |= GK208_VM_PTE_READ_ONLY;
        if ((physical & 0xFFFU) != 0U || physical >= (1ULL << 40U) ||
            !gr_channel_map_pte(window, base, channel->pgt_offset,
                pte, value)) return false;
    }
    for (uint32_t map = 0U; map < 4U; ++map) {
        for (uint32_t page = 0U; page < golden->pte_count[map]; ++page) {
            const uint32_t physical = golden->vram_offset[map] +
                (page << GK208_GR_GPU_PAGE_SHIFT);
            if (!gr_channel_map_pte(window, base, channel->pgt_offset,
                    golden->pte_first[map] + page,
                    ((uint64_t)physical >> 8U) | 1ULL)) return false;
        }
    }
    for (uint32_t page = 0U; page < channel->surface_pte_count; ++page) {
        const uint32_t physical = channel->surface_page_offset +
            (page << GK208_GR_GPU_PAGE_SHIFT);
        if (!gr_channel_map_pte(window, base, channel->pgt_offset,
                channel->surface_pte_first + page,
                ((uint64_t)physical >> 8U) | 1ULL)) return false;
    }
    if (!gr_channel_vram_write64(window, base,
            channel->instance_offset + GK208_GR_INSTANCE_CONTEXT,
            (uint64_t)(golden->gpu_address[3] +
                GK208_GR_GOLDEN_CB_RESERVED) | 4ULL) ||
        !gr_write32(mmio, 0x00100CB8U,
            (channel->pgd_offset >> 12U) << 4U) ||
        !gr_write32(mmio, 0x00100CBCU, 0x80000001U)) return false;
    (void)context;
    (void)prerequisite;
    return gr_wait_mask(mmio, GK208_FB_PAGE_CONFIG, 0x00008000U,
        0x00008000U, 2000U, started, deadline) == 0;
}

static bool gr_channel_write_ramfc(
        const device_domain_region_info_t *window, uint32_t base,
        const gr_channel_memory_plan_t *channel) {
    const uint32_t ramfc = channel->instance_offset;
    return gr_channel_vram_write64(window, base, ramfc + 0x08U,
            channel->userd_offset) &&
        gr_golden_vram_write32(window, base, ramfc + 0x10U, 0x0000FACEU) &&
        gr_golden_vram_write32(window, base, ramfc + 0x30U, 0xFFFFF902U) &&
        gr_channel_vram_write64(window, base, ramfc + 0x48U,
            GK208_CHANNEL_GPFIFO_GPU |
                ((uint64_t)9U << 48U)) &&
        gr_golden_vram_write32(window, base, ramfc + 0x84U, 0x20400000U) &&
        gr_golden_vram_write32(window, base, ramfc + 0x94U, 0x30000001U) &&
        gr_golden_vram_write32(window, base, ramfc + 0x9CU, 0x00000100U) &&
        gr_golden_vram_write32(window, base, ramfc + 0xACU, 0x0000001FU) &&
        gr_golden_vram_write32(window, base, ramfc + 0xB8U, 0xF8000000U) &&
        gr_golden_vram_write32(window, base, ramfc + 0xE4U, 0x00000020U) &&
        gr_golden_vram_write32(window, base, ramfc + 0xE8U,
            GK208_CHANNEL_ID) &&
        gr_golden_vram_write32(window, base, ramfc + 0xF8U, 0x10003080U) &&
        gr_golden_vram_write32(window, base, ramfc + 0xFCU, 0x10000010U);
}

static int gr_channel_find_runlist(
        const device_domain_region_info_t *mmio, uint32_t *runlist_out) {
    uint32_t type = UINT32_MAX;
    uint32_t runlist = UINT32_MAX;
    uint8_t record = 0U;
    for (uint32_t index = 0U; index < GK208_FIFO_TOP_COUNT; ++index) {
        uint32_t data = 0U;
        if (!gr_read32(mmio, GK208_FIFO_TOP_BASE + index * 4U, &data))
            return -5;
        if ((data & 3U) == 0U) continue;
        if (record == 0U) {
            type = UINT32_MAX;
            runlist = UINT32_MAX;
            record = 1U;
        }
        if ((data & 3U) == 2U && (data & 0x10U) != 0U)
            runlist = (data & 0x01E00000U) >> 21U;
        else if ((data & 3U) == 3U)
            type = (data & 0x7FFFFFFCU) >> 2U;
        if ((data & 0x80000000U) != 0U) continue;
        if (type == 0U && runlist < 16U) {
            *runlist_out = runlist;
            return 0;
        }
        record = 0U;
    }
    return -19;
}

static int gr_channel_prepare_bind_region(
        const device_slot_t *device, device_domain_region_info_t *bind) {
    device_domain_region_info_t full;
    const uint32_t bytes = GK208_FIFO_CHANNEL_STRIDE * 2U;
    if (device == NULL || bind == NULL ||
        !platform_ops.describe_region(device->pci_location, 0U, &full) ||
        full.base_high != 0U || full.length_high != 0U ||
        (full.flags & DEVICE_DOMAIN_REGION_MMIO) == 0U ||
        full.length_low < GK208_FIFO_CHANNEL_BASE + bytes ||
        full.base_low > UINT32_MAX - GK208_FIFO_CHANNEL_BASE) return -95;
    *bind = full;
    bind->base_low += GK208_FIFO_CHANNEL_BASE;
    bind->length_low = bytes;
    bind->length_high = 0U;
    return platform_ops.prepare_region(bind) ? 0 : -5;
}

static int gr_channel_wait_runlist(
        const device_domain_region_info_t *mmio, uint32_t runlist,
        uint64_t started, uint64_t deadline) {
    for (uint32_t attempt = 0U; attempt < 2000U; ++attempt) {
        uint32_t pending = 0U;
        if (!gr_read32(mmio, GK208_FIFO_RUNLIST_PENDING_BASE +
                runlist * 8U, &pending)) return -5;
        if ((pending & 0x00100000U) == 0U) return 0;
        if (!gr_wait_one_ms(started, deadline)) return -110;
    }
    return -110;
}

static int gr_channel_commit_runlist(
        const device_domain_region_info_t *mmio, uint32_t runlist,
        uint32_t offset, uint32_t count,
        uint64_t started, uint64_t deadline) {
    if (!gr_write32(mmio, GK208_FIFO_RUNLIST_BASE, offset >> 12U) ||
        !gr_write32(mmio, GK208_FIFO_RUNLIST_SUBMIT,
            (runlist << 20U) | count)) return -5;
    return gr_channel_wait_runlist(mmio, runlist, started, deadline);
}

static int gr_channel_initialize_fifo(
        const device_domain_region_info_t *mmio, uint32_t runlist,
        uint32_t *pbdma_out, uint32_t *original_pbdma_out,
        uint32_t *original_pmc_out) {
    uint32_t pmc = 0U;
    uint32_t original = 0U;
    if (!gr_read32(mmio, GK208_FIFO_PMC_ENABLE, &pmc) ||
        !gr_read32(mmio, GK208_FIFO_PBDMA_ENABLE, &original) ||
        !gr_write32(mmio, GK208_FIFO_PMC_ENABLE,
            pmc | GK208_FIFO_PMC_MASK) ||
        !gr_write32(mmio, GK208_FIFO_PBDMA_ENABLE, UINT32_MAX)) return -5;
    uint32_t supported = 0U;
    if (!gr_read32(mmio, GK208_FIFO_PBDMA_ENABLE, &supported) ||
        !gr_write32(mmio, GK208_FIFO_PBDMA_ENABLE, original) ||
        supported == 0U) return -19;
    uint32_t participating = 0U;
    for (uint32_t id = 0U; id < 32U; ++id) {
        if ((supported & (1U << id)) == 0U) continue;
        uint32_t runlists = 0U;
        if (!gr_read32(mmio, 0x00002390U + id * 4U, &runlists)) return -5;
        if ((runlists & (1U << runlist)) == 0U) continue;
        const uint32_t base = GK208_FIFO_PBDMA_BASE +
            id * GK208_FIFO_PBDMA_STRIDE;
        if (!gr_mask32(mmio, base + 0x3CU, 0x10000100U, 0U) ||
            !gr_write32(mmio, base + 0x08U, UINT32_MAX) ||
            !gr_write32(mmio, base + 0x0CU, 0U) ||
            !gr_write32(mmio, base + 0x48U, UINT32_MAX) ||
            !gr_write32(mmio, base + 0x4CU, 0U) ||
            !gr_write32(mmio, base + 0x2CU, 0x000F4240U)) return -5;
        participating |= 1U << id;
    }
    if (participating == 0U ||
        !gr_write32(mmio, GK208_FIFO_PBDMA_ENABLE, supported) ||
        !gr_mask32(mmio, GK208_FIFO_PBDMA_CONTROL,
            0xBFFFFFFFU, 0xBFFFFFFFU) ||
        !gr_write32(mmio, GK208_FIFO_INTR_STATUS, UINT32_MAX) ||
        !gr_write32(mmio, GK208_FIFO_INTR_ENABLE, 0U) ||
        !gr_mask32(mmio, GK208_FIFO_RUNLIST_BLOCK,
            1U << runlist, 0U)) return -5;
    *pbdma_out = participating;
    *original_pbdma_out = original;
    *original_pmc_out = pmc & GK208_FIFO_PMC_MASK;
    return 0;
}

static uint32_t gr_channel_method_header(
        uint32_t method, uint32_t subchannel, uint32_t count) {
    return (1U << 29U) | (count << 16U) |
        (subchannel << 13U) | (method >> 2U);
}

static bool gr_channel_emit(uint32_t *words, uint32_t *count,
        uint32_t method, uint32_t value) {
    if (*count > GK208_2D_MAX_WORDS - 2U) return false;
    words[(*count)++] = gr_channel_method_header(
        method, GK208_2D_SUBCHANNEL, 1U);
    words[(*count)++] = value;
    return true;
}

static bool gr_channel_emit_surface(uint32_t *words, uint32_t *count,
        const device_slot_t *device, uint32_t gpu, bool source) {
    const uint32_t base = source ? 0x0230U : 0x0200U;
    return gr_channel_emit(words, count, base + 0x00U, 0xE6U) &&
        gr_channel_emit(words, count, base + 0x04U, 1U) &&
        gr_channel_emit(words, count, base + 0x14U,
            device->gr_channel_policy.pitch) &&
        gr_channel_emit(words, count, base + 0x18U,
            device->gr_channel_policy.width) &&
        gr_channel_emit(words, count, base + 0x1CU,
            device->gr_channel_policy.height) &&
        gr_channel_emit(words, count, base + 0x20U, 0U) &&
        gr_channel_emit(words, count, base + 0x24U, gpu);
}

static bool gr_channel_rect_valid(const device_slot_t *device,
        uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    return device != NULL && width != 0U && height != 0U &&
        x < device->gr_channel_policy.width &&
        y < device->gr_channel_policy.height &&
        width <= device->gr_channel_policy.width - x &&
        height <= device->gr_channel_policy.height - y;
}

static int gr_channel_compile_submission(const device_slot_t *device,
        const device_domain_gr_2d_request_t *request,
        uint32_t surface_gpu, uint32_t *words, uint32_t *word_count) {
    if (device == NULL || request == NULL || words == NULL ||
        word_count == NULL || !gr_channel_rect_valid(device,
            request->destination_x, request->destination_y,
            request->width, request->height)) return -34;
    uint32_t count = 0U;
    words[count++] = gr_channel_method_header(0U, GK208_2D_SUBCHANNEL, 1U);
    words[count++] = GK208_2D_CLASS;
    if (!gr_channel_emit_surface(words, &count, device, surface_gpu, false))
        return -28;
    if (request->operation == DEVICE_DOMAIN_GR_2D_RECT_FILL) {
        if ((request->color & 0xFF000000U) != 0U ||
            !gr_channel_emit(words, &count, 0x0290U, 0U) ||
            !gr_channel_emit(words, &count, 0x02ACU, 3U) ||
            !gr_channel_emit(words, &count, 0x0580U, 4U) ||
            !gr_channel_emit(words, &count, 0x0584U, 0xE6U) ||
            !gr_channel_emit(words, &count, 0x0588U, request->color) ||
            !gr_channel_emit(words, &count, 0x0600U,
                request->destination_x) ||
            !gr_channel_emit(words, &count, 0x0604U,
                request->destination_y) ||
            !gr_channel_emit(words, &count, 0x0608U,
                request->destination_x + request->width) ||
            !gr_channel_emit(words, &count, 0x060CU,
                request->destination_y + request->height)) return -28;
    } else if (request->operation == DEVICE_DOMAIN_GR_2D_RECT_COPY) {
        if (!gr_channel_rect_valid(device, request->source_x,
                request->source_y, request->width, request->height) ||
            !gr_channel_emit_surface(words, &count, device,
                surface_gpu, true) ||
            !gr_channel_emit(words, &count, 0x0290U, 0U) ||
            !gr_channel_emit(words, &count, 0x02ACU, 3U) ||
            !gr_channel_emit(words, &count, 0x0888U, 1U) ||
            !gr_channel_emit(words, &count, 0x08B0U,
                request->destination_x) ||
            !gr_channel_emit(words, &count, 0x08B4U,
                request->destination_y) ||
            !gr_channel_emit(words, &count, 0x08B8U, request->width) ||
            !gr_channel_emit(words, &count, 0x08BCU, request->height) ||
            !gr_channel_emit(words, &count, 0x08C0U, 0U) ||
            !gr_channel_emit(words, &count, 0x08C4U, 1U) ||
            !gr_channel_emit(words, &count, 0x08C8U, 0U) ||
            !gr_channel_emit(words, &count, 0x08CCU, 1U) ||
            !gr_channel_emit(words, &count, 0x08D0U, 0U) ||
            !gr_channel_emit(words, &count, 0x08D4U, request->source_x) ||
            !gr_channel_emit(words, &count, 0x08D8U, 0U) ||
            !gr_channel_emit(words, &count, 0x08DCU, request->source_y))
            return -28;
    } else return -22;
    if (count > GK208_2D_MAX_WORDS - 5U) return -28;
    words[count++] = gr_channel_method_header(0x10U, 0U, 4U);
    words[count++] = 0U;
    words[count++] = GK208_CHANNEL_FENCE_GPU;
    words[count++] = request->fence_sequence;
    words[count++] = 0x01000002U;
    *word_count = count;
    return 0;
}

#ifdef REIST_HOST_TEST
static bool gr_channel_test_fence_completion = true;
#endif

static int gr_channel_submit_locked(device_slot_t *device,
        dma_pool_slot_t *pool, const device_domain_gr_2d_request_t *request,
        uint32_t surface_gpu, uint64_t started, uint64_t deadline) {
    if (device == NULL || pool == NULL || request == NULL ||
        device->gr_channel_active == 0U ||
        device->gr_channel_bus_master_active == 0U ||
        request->fence_sequence != device->gr_channel_fence_sequence + 1U)
        return -13;
    uint32_t words[GK208_2D_MAX_WORDS] = {0};
    uint32_t word_count = 0U;
    int status = gr_channel_compile_submission(
        device, request, surface_gpu, words, &word_count);
    if (status != 0) return status;
    const uint32_t pool_index = (uint32_t)(pool - dma_pools);
    uint8_t *storage = dma_pool_storage[pool_index];
    const uint32_t entry = device->gr_channel_gpfifo_put %
        GK208_CHANNEL_GPFIFO_ENTRIES;
    const uint32_t next = (entry + 1U) % GK208_CHANNEL_GPFIFO_ENTRIES;
    uint32_t fence = 0U;
    memcpy(storage + GK208_CHANNEL_FENCE_POOL_OFFSET,
        &fence, sizeof(fence));
    memcpy(storage + GK208_CHANNEL_PUSH_POOL_OFFSET,
        words, word_count * sizeof(uint32_t));
    const uint32_t gpfifo[2] = {
        GK208_CHANNEL_PUSH_GPU,
        word_count << 10U,
    };
    memcpy(storage + GK208_CHANNEL_GPFIFO_POOL_OFFSET + entry * 8U,
        gpfifo, sizeof(gpfifo));
    __sync_synchronize();
    if (!gr_write32(&device->gr_channel_mmio,
            GK208_FIFO_INTR_STATUS, UINT32_MAX)) return -5;
    for (uint32_t id = 0U; id < 32U; ++id) {
        if ((device->gr_channel_pbdma_mask & (1U << id)) == 0U) continue;
        const uint32_t base = GK208_FIFO_PBDMA_BASE +
            id * GK208_FIFO_PBDMA_STRIDE;
        if (!gr_write32(&device->gr_channel_mmio,
                base + 0x08U, UINT32_MAX) ||
            !gr_write32(&device->gr_channel_mmio,
                base + 0x48U, UINT32_MAX)) return -5;
    }
    if (!gr_golden_vram_write32(&device->gr_channel_vram_window,
            device->gr_channel_instance_offset,
            device->gr_channel_userd_offset + GK208_FIFO_USERD_PUT,
            next)) return -5;
#ifdef REIST_HOST_TEST
    if (gr_channel_test_fence_completion)
        memcpy(storage + GK208_CHANNEL_FENCE_POOL_OFFSET,
            &request->fence_sequence, sizeof(request->fence_sequence));
#endif
    for (uint32_t attempt = 0U; attempt < 500U; ++attempt) {
        __sync_synchronize();
        memcpy(&fence, storage + GK208_CHANNEL_FENCE_POOL_OFFSET,
            sizeof(fence));
        if (fence == request->fence_sequence) {
            uint32_t fifo_fault = 0U;
            uint32_t graph_fault = 0U;
            if (!gr_read32(&device->gr_channel_mmio,
                    GK208_FIFO_INTR_STATUS, &fifo_fault) ||
                !gr_read32(&device->gr_channel_mmio,
                    0x00400100U, &graph_fault) ||
                fifo_fault != 0U || graph_fault != 0U) return -5;
            for (uint32_t id = 0U; id < 32U; ++id) {
                if ((device->gr_channel_pbdma_mask & (1U << id)) == 0U)
                    continue;
                const uint32_t base = GK208_FIFO_PBDMA_BASE +
                    id * GK208_FIFO_PBDMA_STRIDE;
                uint32_t pbdma_fault = 0U;
                uint32_t pbdma_state = 0U;
                if (!gr_read32(&device->gr_channel_mmio,
                        base + 0x08U, &pbdma_fault) ||
                    !gr_read32(&device->gr_channel_mmio,
                        base + 0x48U, &pbdma_state) ||
                    pbdma_fault != 0U || pbdma_state != 0U) return -5;
            }
            device->gr_channel_fence_sequence = request->fence_sequence;
            device->gr_channel_gpfifo_put = next;
            return 0;
        }
        if (!gr_wait_one_ms(started, deadline)) return -110;
    }
    return -110;
}

static bool gr_channel_hardware_stop(device_slot_t *device) {
    if (device == NULL || device->gr_channel_bus_master_active == 0U)
        return true;
    device_domain_region_info_t bind;
    const uint64_t started = platform_ops.monotonic_ms();
    const uint64_t deadline = started > UINT64_MAX - 2000U
        ? UINT64_MAX : started + 2000U;
    bool clean = true;
    const bool bind_ready = gr_channel_prepare_bind_region(device, &bind) == 0;
    if (!bind_ready) clean = false;
    uint32_t control = 0U;
    if (bind_ready && (!gr_read32(&bind,
            GK208_CHANNEL_ID * GK208_FIFO_CHANNEL_STRIDE + 4U, &control) ||
        !gr_write32(&bind,
            GK208_CHANNEL_ID * GK208_FIFO_CHANNEL_STRIDE + 4U,
            control | 0x00000800U))) clean = false;
    if (!gr_golden_vram_write32(&device->gr_channel_vram_window,
            device->gr_channel_instance_offset,
            device->gr_channel_runlist_offset, 0U) ||
        !gr_golden_vram_write32(&device->gr_channel_vram_window,
            device->gr_channel_instance_offset,
            device->gr_channel_runlist_offset + 4U, 0U)) clean = false;
    if (gr_channel_commit_runlist(&device->gr_channel_mmio,
            device->gr_channel_runlist_id,
            device->gr_channel_runlist_offset, 0U,
            started, deadline) != 0) clean = false;
    if (bind_ready && !gr_write32(&bind,
            GK208_CHANNEL_ID * GK208_FIFO_CHANNEL_STRIDE, 0U)) clean = false;
    if (!gr_write32(&device->gr_channel_mmio, GK208_FIFO_PBDMA_ENABLE,
            device->gr_channel_original_pbdma_mask)) clean = false;
    if (!gr_mask32(&device->gr_channel_mmio, GK208_FIFO_PMC_ENABLE,
            GK208_FIFO_PMC_MASK,
            device->gr_channel_original_pmc_bits)) clean = false;
    return clean;
}

static bool gr_channel_scrub_private(device_slot_t *device) {
    if (device == NULL || device->gr_channel_private_end == 0U) return true;
    bool clean = true;
    for (uint32_t offset = device->gr_channel_instance_offset;
         clean && offset < device->gr_channel_private_end;
         offset += sizeof(uint32_t))
        clean = gr_golden_vram_write32(&device->gr_channel_vram_window,
            device->gr_channel_instance_offset, offset, 0U);
    return clean;
}

static int gr_channel_validate_owner_locked(
        int pid, uint32_t generation,
        const device_domain_gr_channel_request_t *request,
        device_slot_t **device_out, dma_pool_slot_t **pool_out) {
    device_slot_t *device = owned_slot(pid, generation, request->device);
    resource_slot_t *region = owned_resource_locked(pid, generation,
        request->region, DEVICE_DOMAIN_RESOURCE_REGION);
    resource_slot_t *dma = owned_resource_locked(pid, generation,
        request->dma, DEVICE_DOMAIN_RESOURCE_DMA);
    dma_pool_slot_t *pool = dma_pool_for_resource(dma);
    if (device == NULL || region == NULL || dma == NULL) return -9;
    const uint32_t index = (uint32_t)(device - devices);
    if (region->device_slot != index || dma->device_slot != index ||
        region->device_generation != device->generation ||
        dma->device_generation != device->generation ||
        device->state != DEVICE_DOMAIN_DMA_BOUND || pool == NULL ||
        pool->sealed == 0U || request->policy_id == 0U ||
        device->gr_channel_policy_installed == 0U ||
        request->policy_id != device->gr_channel_policy.policy_id ||
        region->region.region_index !=
            device->gr_prerequisite_policy.region_index ||
        (region->region.rights & DEVICE_DOMAIN_REGION_ACCESS_READ) == 0U)
        return -13;
    *device_out = device;
    *pool_out = pool;
    return 0;
}

int device_domain_gr_channel_activate(
        int pid, uint32_t process_generation,
        const device_domain_gr_channel_request_t *request,
        device_domain_gr_channel_result_t *result) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        request == NULL || result == NULL ||
        request->version != DEVICE_DOMAIN_ABI_VERSION ||
        request->struct_size != sizeof(*request) || request->device == 0U ||
        request->region == 0U || request->dma == 0U ||
        request->flags != 0U || request->reserved[0] != 0U ||
        request->reserved[1] != 0U || request->reserved[2] != 0U)
        return -22;
    if (!begin_operation()) return -16;
    device_slot_t *device = NULL;
    dma_pool_slot_t *pool = NULL;
    int status = gr_channel_validate_owner_locked(pid, process_generation,
        request, &device, &pool);
    if (status != 0 || device->gr_golden_context_active == 0U ||
        device->gr_channel_active != 0U) {
        end_operation();
        return status != 0 ? status : -13;
    }
    const device_domain_gr_prerequisite_policy_t *prerequisite =
        &device->gr_prerequisite_policy;
    const uint8_t *storage = dma_pool_storage[
        (uint32_t)(pool - dma_pools)];
    gr_prerequisite_topology_t topology;
    gr_prerequisite_plan_t prerequisite_plan;
    gr_context_memory_plan_t context;
    gr_golden_memory_plan_t golden;
    gr_channel_memory_plan_t channel;
    device_domain_region_info_t vram;
    device_domain_region_info_t window;
    uint32_t image_crc = 0U;
    bool valid = gr_sample_topology(&device->gr_firmware_region, &topology) &&
        gr_execution_image_valid(storage, pool->capacity, prerequisite,
            &device->gr_firmware_region, &topology, &image_crc) &&
        gr_build_prerequisite_plan(&device->gr_firmware_region,
            prerequisite, &prerequisite_plan) &&
        gr_plan_matches_device(&prerequisite_plan, device) &&
        image_crc == device->gr_prerequisite_image_crc &&
        gr_build_context_memory_plan(&topology, &prerequisite_plan,
            prerequisite, device->gr_execution_context_size, &context) &&
        gr_golden_build_memory_plan(&context, &prerequisite_plan,
            prerequisite, &golden) &&
        gr_channel_build_memory_plan(&golden, prerequisite,
            &device->gr_channel_policy, &channel) &&
        platform_ops.describe_region(device->pci_location,
            prerequisite->vram_region_index, &vram) &&
        vram.base_high == 0U && vram.length_high == 0U &&
        vram.length_low == prerequisite->vram_aperture_bytes &&
        channel.end_offset <= vram.length_low &&
        vram.base_low <= UINT32_MAX - channel.instance_offset;
    if (!valid) {
        end_operation();
        return -84;
    }
    window = vram;
    window.base_low += channel.instance_offset;
    window.length_low = channel.end_offset - channel.instance_offset;
    if (!platform_ops.prepare_region(&window)) {
        end_operation();
        return -5;
    }
    const uint64_t started = platform_ops.monotonic_ms();
    const uint64_t deadline = started >
            UINT64_MAX - DEVICE_DOMAIN_GR_EXECUTION_TIMEOUT_MS
        ? UINT64_MAX : started + DEVICE_DOMAIN_GR_EXECUTION_TIMEOUT_MS;
    uint32_t runlist = 0U;
    uint32_t pbdma = 0U;
    uint32_t original_pbdma = 0U;
    uint32_t original_pmc = 0U;
    device_domain_region_info_t bind;
    status = gr_channel_zero_private(&window, channel.instance_offset,
        &channel, started, deadline) ? 0 : -5;
    if (status == 0 && !gr_channel_install_vm(&device->gr_firmware_region,
            &window, channel.instance_offset, &golden, &context, &channel,
            prerequisite, (uint32_t)(pool - dma_pools), started, deadline))
        status = -5;
    if (status == 0 && !gr_channel_write_ramfc(
            &window, channel.instance_offset, &channel)) status = -5;
    if (status == 0) status = gr_channel_find_runlist(
        &device->gr_firmware_region, &runlist);
    if (status == 0) status = gr_channel_prepare_bind_region(device, &bind);
    if (status == 0) status = gr_channel_initialize_fifo(
        &device->gr_firmware_region, runlist, &pbdma,
        &original_pbdma, &original_pmc);
    if (status == 0 && (!gr_golden_vram_write32(&window,
            channel.instance_offset, channel.runlist_offset,
            GK208_CHANNEL_ID) ||
        !gr_golden_vram_write32(&window, channel.instance_offset,
            channel.runlist_offset + 4U, 0U))) status = -5;
    device->gr_channel_instance_offset = channel.instance_offset;
    device->gr_channel_pgd_offset = channel.pgd_offset;
    device->gr_channel_pgt_offset = channel.pgt_offset;
    device->gr_channel_userd_offset = channel.userd_offset;
    device->gr_channel_runlist_offset = channel.runlist_offset;
    device->gr_channel_private_end = channel.end_offset;
    device->gr_channel_runlist_id = runlist;
    device->gr_channel_pbdma_mask = pbdma;
    device->gr_channel_original_pbdma_mask = original_pbdma;
    device->gr_channel_original_pmc_bits = original_pmc;
    device->gr_channel_mmio = device->gr_firmware_region;
    device->gr_channel_vram_window = window;
    if (status == 0 && !platform_ops.set_bus_master(
            device->pci_location, true)) status = -5;
    if (status == 0) device->gr_channel_bus_master_active = 1U;
    uint32_t control = 0U;
    if (status == 0 && (!gr_read32(&bind,
            GK208_CHANNEL_ID * GK208_FIFO_CHANNEL_STRIDE + 4U, &control) ||
        !gr_write32(&bind,
            GK208_CHANNEL_ID * GK208_FIFO_CHANNEL_STRIDE + 4U,
            (control & ~0x000F0000U) | (runlist << 16U)) ||
        !gr_write32(&bind,
            GK208_CHANNEL_ID * GK208_FIFO_CHANNEL_STRIDE,
            0x80000000U | (channel.instance_offset >> 12U)))) status = -5;
    if (status == 0) status = gr_channel_commit_runlist(
        &device->gr_firmware_region, runlist, channel.runlist_offset,
        1U, started, deadline);
    if (status == 0 && (!gr_read32(&bind,
            GK208_CHANNEL_ID * GK208_FIFO_CHANNEL_STRIDE + 4U, &control) ||
        !gr_write32(&bind,
            GK208_CHANNEL_ID * GK208_FIFO_CHANNEL_STRIDE + 4U,
            control | 0x00000400U))) status = -5;
    if (status == 0) device->gr_channel_active = 1U;
    device_domain_gr_2d_request_t self_test = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(self_test),
        .device = request->device,
        .region = request->region,
        .dma = request->dma,
        .policy_id = request->policy_id,
        .operation = DEVICE_DOMAIN_GR_2D_RECT_FILL,
        .fence_sequence = 1U,
        .destination_x = device->gr_channel_policy.width - 1U,
        .destination_y = device->gr_channel_policy.height - 1U,
        .width = 1U,
        .height = 1U,
    };
    if (status == 0) status = gr_channel_submit_locked(
        device, pool, &self_test, channel.surface_gpu_address,
        started, deadline);
    self_test.operation = DEVICE_DOMAIN_GR_2D_RECT_COPY;
    self_test.fence_sequence = 2U;
    self_test.source_x = self_test.destination_x;
    self_test.source_y = self_test.destination_y;
    if (status == 0) status = gr_channel_submit_locked(
        device, pool, &self_test, channel.surface_gpu_address,
        started, deadline);
    if (status != 0) {
        const bool stopped = gr_channel_hardware_stop(device);
        const bool disabled = platform_ops.set_bus_master(
            device->pci_location, false);
        const bool scrubbed = disabled && gr_channel_scrub_private(device);
        clear_gr_channel_state(device);
        if (!stopped || !disabled || !scrubbed) status = -5;
        status = gr_execution_rollback(device, status);
        end_operation();
        return status;
    }
    *result = (device_domain_gr_channel_result_t){
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(*result),
        .device = request->device,
        .policy_id = request->policy_id,
        .width = device->gr_channel_policy.width,
        .height = device->gr_channel_policy.height,
        .pitch = device->gr_channel_policy.pitch,
        .capabilities = GK208_CHANNEL_CAPABILITIES,
        .fence_sequence = device->gr_channel_fence_sequence,
    };
    end_operation();
    return 0;
}

int device_domain_gr_2d_submit(
        int pid, uint32_t process_generation,
        const device_domain_gr_2d_request_t *request,
        device_domain_gr_2d_result_t *result) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        request == NULL || result == NULL ||
        request->version != DEVICE_DOMAIN_ABI_VERSION ||
        request->struct_size != sizeof(*request) || request->device == 0U ||
        request->region == 0U || request->dma == 0U ||
        (request->operation != DEVICE_DOMAIN_GR_2D_RECT_FILL &&
         request->operation != DEVICE_DOMAIN_GR_2D_RECT_COPY) ||
        request->fence_sequence == 0U || request->flags != 0U ||
        request->reserved[0] != 0U || request->reserved[1] != 0U ||
        request->reserved[2] != 0U || request->reserved[3] != 0U)
        return -22;
    if (!begin_operation()) return -16;
    const device_domain_gr_channel_request_t common = {
        .version = request->version,
        .struct_size = sizeof(common),
        .device = request->device,
        .region = request->region,
        .dma = request->dma,
        .policy_id = request->policy_id,
    };
    device_slot_t *device = NULL;
    dma_pool_slot_t *pool = NULL;
    int status = gr_channel_validate_owner_locked(pid, process_generation,
        &common, &device, &pool);
    if (status == 0 && device->gr_channel_active == 0U) status = -13;
    const uint32_t page_offset =
        device != NULL ? device->gr_prerequisite_policy.scanout_offset &
            0xFFFU : 0U;
    const uint32_t surface = GK208_CHANNEL_SURFACE_GPU + page_offset;
    const uint64_t started = platform_ops.monotonic_ms();
    const uint64_t deadline = started > UINT64_MAX - 500U
        ? UINT64_MAX : started + 500U;
    if (status == 0) status = gr_channel_submit_locked(
        device, pool, request, surface, started, deadline);
    if (status != 0 && status != -22 && status != -34 && device != NULL) {
        const bool stopped = gr_channel_hardware_stop(device);
        const bool disabled = platform_ops.set_bus_master(
            device->pci_location, false);
        const bool scrubbed = disabled && gr_channel_scrub_private(device);
        clear_gr_channel_state(device);
        if (!stopped || !disabled || !scrubbed) status = -5;
    }
    if (status == 0) {
        *result = (device_domain_gr_2d_result_t){
            .version = DEVICE_DOMAIN_ABI_VERSION,
            .struct_size = sizeof(*result),
            .device = request->device,
            .policy_id = request->policy_id,
            .fence_sequence = request->fence_sequence,
            .capabilities = GK208_CHANNEL_CAPABILITIES,
        };
    }
    end_operation();
    return status;
}

int device_domain_gr_channel_deactivate(
        int pid, uint32_t process_generation,
        const device_domain_gr_channel_request_t *request) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        request == NULL || request->version != DEVICE_DOMAIN_ABI_VERSION ||
        request->struct_size != sizeof(*request) || request->device == 0U ||
        request->region == 0U || request->dma == 0U ||
        request->flags != 0U || request->reserved[0] != 0U ||
        request->reserved[1] != 0U || request->reserved[2] != 0U)
        return -22;
    if (!begin_operation()) return -16;
    device_slot_t *device = NULL;
    dma_pool_slot_t *pool = NULL;
    int status = gr_channel_validate_owner_locked(pid, process_generation,
        request, &device, &pool);
    (void)pool;
    if (status == 0 && device->gr_channel_active != 0U) {
        const bool stopped = gr_channel_hardware_stop(device);
        const bool disabled = platform_ops.set_bus_master(
            device->pci_location, false);
        const bool scrubbed = disabled && gr_channel_scrub_private(device);
        clear_gr_channel_state(device);
        if (!stopped || !disabled || !scrubbed) status = -5;
    }
    end_operation();
    return status;
}

bool device_domain_gr_acceleration_active(void) {
    if (!initialized || !begin_operation()) return false;
    bool active = false;
    for (uint32_t index = 0U; index < device_count; ++index) {
        const device_slot_t *device = &devices[index];
        if (device->registered != 0U && device->owner_pid > 0 &&
            device->owner_generation != 0U &&
            device->gr_channel_active != 0U &&
            device->gr_channel_bus_master_active != 0U &&
            device->gr_channel_policy_installed != 0U) {
            active = true;
            break;
        }
    }
    end_operation();
    return active;
}

static const device_domain_region_rule_t *region_write_rule(
        const device_slot_t *device, uint32_t region_index, uint32_t offset,
        uint32_t width, uint32_t kind) {
    if (device == NULL || device->region_policy_installed == 0U) return NULL;
    for (uint32_t index = 0U; index < device->region_policy.rule_count;
         ++index) {
        const device_domain_region_rule_t *rule =
            &device->region_policy.rules[index];
        if (rule->region_index == region_index && rule->offset == offset &&
            rule->width == width && rule->kind == kind) return rule;
    }
    return NULL;
}

int device_domain_region_read(int pid, uint32_t process_generation,
                              const device_domain_region_access_t *request,
                              device_domain_region_value_t *result) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        request == NULL || result == NULL ||
        request->version != DEVICE_DOMAIN_ABI_VERSION ||
        request->struct_size != sizeof(*request) || request->region == 0U ||
        (request->width != 1U && request->width != 2U &&
         request->width != 4U) || request->value != 0U ||
        request->flags != 0U || request->reserved != 0U) return -22;
    if (!begin_operation()) return -16;
    resource_slot_t *resource = owned_resource_locked(
        pid, process_generation, request->region,
        DEVICE_DOMAIN_RESOURCE_REGION);
    if (resource == NULL ||
        (resource->region.rights & DEVICE_DOMAIN_REGION_ACCESS_READ) == 0U) {
        end_operation();
        return resource == NULL ? -9 : -13;
    }
    device_slot_t *device = &devices[resource->device_slot];
    uint32_t readable = device->region_policy.readable_bytes[
        resource->region.region_index];
    if (request->offset > readable || request->width > readable -
            request->offset || (request->offset & (request->width - 1U)) != 0U) {
        end_operation();
        return -22;
    }
    uint32_t value = 0U;
    if (!platform_ops.read_region(
            &resource->region, request->offset, request->width, &value)) {
        end_operation();
        return -5;
    }
    *result = (device_domain_region_value_t){
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(*result),
        .region = request->region,
        .offset = request->offset,
        .width = request->width,
        .value = value,
    };
    end_operation();
    return 0;
}

int device_domain_region_write(int pid, uint32_t process_generation,
                               const device_domain_region_access_t *request) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        request == NULL || request->version != DEVICE_DOMAIN_ABI_VERSION ||
        request->struct_size != sizeof(*request) || request->region == 0U ||
        (request->width != 1U && request->width != 2U &&
         request->width != 4U) || request->flags != 0U ||
        request->reserved != 0U) return -22;
    if (!begin_operation()) return -16;
    resource_slot_t *resource = owned_resource_locked(
        pid, process_generation, request->region,
        DEVICE_DOMAIN_RESOURCE_REGION);
    if (resource == NULL ||
        (resource->region.rights & DEVICE_DOMAIN_REGION_ACCESS_WRITE) == 0U) {
        end_operation();
        return resource == NULL ? -9 : -13;
    }
    device_slot_t *device = &devices[resource->device_slot];
    const device_domain_region_rule_t *rule = region_write_rule(
        device, resource->region.region_index, request->offset,
        request->width, DEVICE_DOMAIN_REGION_RULE_VALUE);
    if (rule == NULL || (request->value & ~rule->writable_mask) != 0U) {
        end_operation();
        return -13;
    }
    if (!platform_ops.write_region(&resource->region, request->offset,
                                   request->width, request->value)) {
        (void)fence_slot(device);
        end_operation();
        return -5;
    }
    end_operation();
    return 0;
}

int device_domain_region_bind_dma(
        int pid, uint32_t process_generation,
        const device_domain_region_dma_address_t *request) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        request == NULL || request->version != DEVICE_DOMAIN_ABI_VERSION ||
        request->struct_size != sizeof(*request) || request->region == 0U ||
        request->dma == 0U || request->flags != 0U ||
        request->reserved != 0U ||
        (request->buffer_offset &
         (DEVICE_DOMAIN_DMA_ADDRESS_ALIGNMENT - 1U)) != 0U) return -22;
    if (!begin_operation()) return -16;
    resource_slot_t *region = owned_resource_locked(
        pid, process_generation, request->region,
        DEVICE_DOMAIN_RESOURCE_REGION);
    resource_slot_t *dma = owned_resource_locked(
        pid, process_generation, request->dma,
        DEVICE_DOMAIN_RESOURCE_DMA);
    if (region == NULL || dma == NULL ||
        region->device_slot != dma->device_slot ||
        region->device_generation != dma->device_generation ||
        (region->region.rights & DEVICE_DOMAIN_REGION_ACCESS_WRITE) == 0U) {
        end_operation();
        return region == NULL || dma == NULL ? -9 : -13;
    }
    device_slot_t *device = &devices[region->device_slot];
    const device_domain_region_rule_t *rule = region_write_rule(
        device, region->region.region_index, request->register_offset, 8U,
        DEVICE_DOMAIN_REGION_RULE_DMA_ADDRESS);
    bool descriptor_address = false;
    if (rule == NULL) {
        rule = region_write_rule(
            device, region->region.region_index, request->register_offset, 8U,
            DEVICE_DOMAIN_REGION_RULE_DMA_DESCRIPTOR_ADDRESS);
        descriptor_address = rule != NULL;
    }
    dma_pool_slot_t *pool = dma_pool_for_resource(dma);
    if (rule == NULL || pool == NULL) {
        end_operation();
        return rule == NULL ? -13 : -95;
    }
    if (request->buffer_offset >= pool->capacity) {
        end_operation();
        return -22;
    }
    if ((descriptor_address && request->buffer_offset != 0U) ||
        (!descriptor_address &&
         request->buffer_offset < DEVICE_DOMAIN_DMA_DATA_OFFSET)) {
        end_operation();
        return -13;
    }
    uint32_t pool_index = dma->platform_capability - 1U;
    uint64_t address = dma_pool_physical_address(
        pool_index, request->buffer_offset);
    uint32_t address_low = (uint32_t)address;
    uint32_t address_high = (uint32_t)(address >> 32U);
    if (!platform_ops.write_dma_address(
            &region->region, request->register_offset,
            address_low, address_high)) {
        (void)fence_slot(device);
        end_operation();
        return -5;
    }
    end_operation();
    return 0;
}

int device_domain_resource_status(int pid, uint32_t process_generation,
                                  device_domain_resource_handle_t handle,
                                  device_domain_resource_status_t *status) {
    uint32_t slot = 0U;
    uint32_t generation = 0U;
    if (!initialized || pid <= 0 || process_generation == 0U ||
        status == NULL ||
        !decode_resource_handle(handle, &slot, &generation)) return -22;
    if (!begin_operation()) return -16;
    const resource_slot_t *resource = &resources[slot];
    if (resource->active == 0U || resource->generation != generation ||
        resource->owner_pid != pid ||
        resource->owner_generation != process_generation ||
        resource->device_slot >= device_count) {
        end_operation();
        return -9;
    }
    memset(status, 0, sizeof(*status));
    status->version = DEVICE_DOMAIN_ABI_VERSION;
    status->struct_size = sizeof(*status);
    status->kind = resource->kind;
    status->generation = resource->generation;
    status->device = make_handle(resource->device_slot,
                                  resource->device_generation);
    status->owner_pid = resource->owner_pid;
    status->owner_generation = resource->owner_generation;
    status->platform_capability = resource->platform_capability;
    end_operation();
    return 0;
}

#ifdef REIST_HOST_TEST
void device_domain_test_raise_irq(uint8_t irq) {
    if (irq < PCI_LEGACY_IRQ_COUNT)
        (void)__sync_fetch_and_or(&pending_irq_lines, 1U << irq);
}

bool device_domain_test_dma_word(device_domain_resource_handle_t handle,
                                 uint32_t offset, uint64_t *value) {
    uint32_t resource_index = 0U;
    uint32_t generation = 0U;
    if (value == NULL || !decode_resource_handle(
            handle, &resource_index, &generation))
        return false;
    resource_slot_t *resource = &resources[resource_index];
    if (resource->active == 0U || resource->generation != generation ||
        resource->kind != DEVICE_DOMAIN_RESOURCE_DMA ||
        resource->platform_capability == 0U ||
        resource->platform_capability > DEVICE_DOMAIN_DMA_POOL_COUNT)
        return false;
    dma_pool_slot_t *pool =
        &dma_pools[resource->platform_capability - 1U];
    if (pool->active == 0U || offset > pool->capacity - sizeof(*value))
        return false;
    memcpy(value, &dma_pool_storage[resource->platform_capability - 1U]
                                  [offset], sizeof(*value));
    return true;
}

void device_domain_test_set_gr_fence_completion(bool enabled) {
    gr_channel_test_fence_completion = enabled;
}

void device_domain_test_reset(void) {
    memset(devices, 0, sizeof(devices));
    memset(groups, 0, sizeof(groups));
    memset(resources, 0, sizeof(resources));
    memset(dma_pools, 0, sizeof(dma_pools));
    memset(dma_pool_storage, 0, sizeof(dma_pool_storage));
    memset(&dma_pool_stats, 0, sizeof(dma_pool_stats));
    memset(irq_line_bindings, 0, sizeof(irq_line_bindings));
    memset(&platform_ops, 0, sizeof(platform_ops));
    device_count = 0U;
    platform_iommu_ready = false;
    memset(&iommu_status, 0, sizeof(iommu_status));
    operation_busy = 0U;
    pending_irq_lines = 0U;
    initialized = false;
    gr_channel_test_fence_completion = true;
}
#endif
