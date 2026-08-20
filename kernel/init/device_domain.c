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
#ifndef REIST_HOST_TEST
#include "arch/x86/mm/paging.h"
#include "drivers/char/io.h"
#include "lib/libc/stdio.h"
#include "kernel/time/pit.h"
#endif

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
    uint32_t irq_capability;
    uint32_t dma_capability;
    device_domain_resource_handle_t irq_resource;
    device_domain_resource_handle_t dma_resource;
    uint32_t irq_sequence;
    uint32_t irq_pending_count;
    uint8_t irq_notification_sent;
    uint64_t irq_deadline_ms;
    uint8_t region_policy_installed;
    device_domain_region_policy_t region_policy;
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
    uint32_t device_slot;
    uint32_t device_generation;
    int32_t owner_pid;
    uint32_t owner_generation;
    uint32_t direction;
} dma_pool_slot_t;

static device_slot_t devices[DEVICE_DOMAIN_MAX_DEVICES];
static group_owner_t groups[DEVICE_DOMAIN_MAX_GROUPS];
static resource_slot_t resources[DEVICE_DOMAIN_MAX_RESOURCES];
static dma_pool_slot_t dma_pools[DEVICE_DOMAIN_DMA_POOL_COUNT];
static uint8_t dma_pool_storage[DEVICE_DOMAIN_DMA_POOL_COUNT]
                               [DEVICE_DOMAIN_DMA_POOL_BYTES]
    __attribute__((aligned(4096)));
static device_domain_platform_ops_t platform_ops;
static uint32_t device_count;
static bool initialized;
static bool platform_iommu_ready;
static volatile uint32_t operation_busy;
static device_domain_iommu_status_t iommu_status;
static uint32_t irq_line_bindings[PCI_LEGACY_IRQ_COUNT];
static volatile uint32_t pending_irq_lines;

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
        DEVICE_DOMAIN_PROFILE_LEGACY_INTX_PIC;
    return profile != NULL && profile->version == DEVICE_DOMAIN_ABI_VERSION &&
        profile->struct_size == sizeof(*profile) &&
        profile->isolation_group < DEVICE_DOMAIN_MAX_GROUPS &&
        profile->isolation_group != 0U && profile->vendor_id != 0U &&
        profile->vendor_id != 0xFFFFU && profile->device_id != 0xFFFFU &&
        (profile->flags & ~known_flags) == 0U &&
        ((profile->flags & DEVICE_DOMAIN_PROFILE_LEGACY_INTX_PIC) == 0U ||
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
        memset(dma_pool_storage[index], 0, DEVICE_DOMAIN_DMA_POOL_BYTES);
        *pool = (dma_pool_slot_t){0};
    }
}

static bool mode_allowed(const device_slot_t *device, uint32_t mode) {
    if (mode == DEVICE_DOMAIN_MODE_MEDIATED)
        return (device->profile.flags & DEVICE_DOMAIN_PROFILE_MEDIATED_DMA) != 0U;
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

static bool unmask_device_irq(const device_slot_t *device) {
    if (device == NULL) return false;
    if (device->irq_pic_fallback == 0U)
        return platform_ops.unmask_irq(device->pci_location);
    /* Clear INTx-disable if the endpoint implements it, then release the
     * line-level mask selected by the immutable profile. */
    return platform_ops.unmask_irq(device->pci_location) &&
        pic_unmask_location(device->pci_location);
}

static bool fence_slot(device_slot_t *device) {
    bool irq_masked = mask_device_irq(device);
    bool mastering_disabled =
        platform_ops.set_bus_master(device->pci_location, false);
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
    retire_device_resources((uint32_t)(device - devices), device->generation);
    device->state = DEVICE_DOMAIN_FENCED;
    return irq_masked && mastering_disabled && irq_revoked && dma_revoked;
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
    bool intx_disabled = platform_ops.mask_irq(pci_location);
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
        if (!platform_ops.describe_region(
                device->pci_location, region, &regions[region]) ||
            regions[region].length_high != 0U ||
            regions[region].length_low == 0U ||
            regions[region].length_low > DEVICE_DOMAIN_MAX_REGION_BYTES ||
            policy->readable_bytes[region] > regions[region].length_low) {
            valid = false;
            break;
        }
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
        memset(dma_pool_storage[pool_index], 0, DEVICE_DOMAIN_DMA_POOL_BYTES);
        dma_pools[pool_index] = (dma_pool_slot_t){
            .active = 1U,
            .device_slot = (uint32_t)(device - devices),
            .device_generation = device->generation,
            .owner_pid = pid,
            .owner_generation = process_generation,
            .direction = request->flags,
        };
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
    if (device->state == DEVICE_DOMAIN_FENCED) {
        end_operation();
        return 0;
    }
    bool fenced = fence_slot(device);
    end_operation();
    return fenced ? 0 : -5;
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
    bool fenced = device->state == DEVICE_DOMAIN_FENCED
        ? true : fence_slot(device);
    bool reset = fenced && platform_ops.monotonic_ms() < deadline_ms &&
        platform_ops.reset(device->pci_location, deadline_ms) &&
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
        if (platform_ops.monotonic_ms() >= deadline_ms ||
            !platform_ops.reset(device->pci_location, deadline_ms)) {
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
            if (!mask_device_irq(device) ||
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
        .capacity = DEVICE_DOMAIN_DMA_POOL_BYTES,
        .alignment = 4096U,
        .direction = pool->direction,
    };
    end_operation();
    return 0;
}

static int dma_transfer_locked(int pid, uint32_t process_generation,
        device_domain_resource_handle_t handle, uint32_t offset, void *data,
        uint32_t length, bool write_to_device) {
    if (data == NULL || length == 0U ||
        length > DEVICE_DOMAIN_DMA_TRANSFER_MAX ||
        offset < DEVICE_DOMAIN_DMA_DATA_OFFSET ||
        offset > DEVICE_DOMAIN_DMA_POOL_BYTES ||
        length > DEVICE_DOMAIN_DMA_POOL_BYTES - offset) return -22;
    resource_slot_t *resource = owned_resource_locked(
        pid, process_generation, handle, DEVICE_DOMAIN_RESOURCE_DMA);
    dma_pool_slot_t *pool = dma_pool_for_resource(resource);
    if (pool == NULL) return resource == NULL ? -9 : -95;
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
        request->buffer_offset >= DEVICE_DOMAIN_DMA_POOL_BYTES ||
        request->length >
            DEVICE_DOMAIN_DMA_POOL_BYTES - request->buffer_offset ||
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
    uint64_t address = (uint64_t)(uintptr_t)&dma_pool_storage[pool_index]
                                                   [request->buffer_offset];
    memcpy(&dma_pool_storage[pool_index][descriptor_offset], &address,
           sizeof(address));
    memcpy(&dma_pool_storage[pool_index][descriptor_offset + 8U],
           &request->length, sizeof(request->length));
    memcpy(&dma_pool_storage[pool_index][descriptor_offset + 12U],
           &request->flags, sizeof(request->flags));
    end_operation();
    return 0;
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
         (DEVICE_DOMAIN_DMA_ADDRESS_ALIGNMENT - 1U)) != 0U ||
        request->buffer_offset >= DEVICE_DOMAIN_DMA_POOL_BYTES) return -22;
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
    if ((descriptor_address && request->buffer_offset != 0U) ||
        (!descriptor_address &&
         request->buffer_offset < DEVICE_DOMAIN_DMA_DATA_OFFSET)) {
        end_operation();
        return -13;
    }
    uint32_t pool_index = dma->platform_capability - 1U;
    uintptr_t address = (uintptr_t)&dma_pool_storage[pool_index]
                                                 [request->buffer_offset];
    uint32_t address_low = (uint32_t)address;
    uint32_t address_high = sizeof(uintptr_t) > sizeof(uint32_t)
        ? (uint32_t)((uint64_t)address >> 32U) : 0U;
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

void device_domain_test_reset(void) {
    memset(devices, 0, sizeof(devices));
    memset(groups, 0, sizeof(groups));
    memset(resources, 0, sizeof(resources));
    memset(dma_pools, 0, sizeof(dma_pools));
    memset(dma_pool_storage, 0, sizeof(dma_pool_storage));
    memset(irq_line_bindings, 0, sizeof(irq_line_bindings));
    memset(&platform_ops, 0, sizeof(platform_ops));
    device_count = 0U;
    platform_iommu_ready = false;
    memset(&iommu_status, 0, sizeof(iommu_status));
    operation_busy = 0U;
    pending_irq_lines = 0U;
    initialized = false;
}
#endif
