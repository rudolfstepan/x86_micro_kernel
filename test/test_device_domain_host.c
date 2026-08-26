#include "include/kernel/device_domain.h"
#include "include/kernel/ipc.h"
#include "arch/x86/include/sys.h"
#include "drivers/bus/pci.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static uint32_t mask_calls;
static uint32_t disable_calls;
static uint32_t enable_calls;
static uint32_t unmask_calls;
static uint32_t reset_calls;
static uint32_t irq_revoke_calls;
static uint32_t dma_revoke_calls;
static bool reset_result = true;
static bool mask_result = true;
static bool bus_master_enable_result = true;
static bool bus_master_disable_result = true;
static uint64_t fake_now_ms = 10U;
static uint32_t kernel_notifications;
static device_domain_irq_message_t last_notification;
static uint32_t region_write_calls;
static uint32_t dma_address_writes;
static uint32_t last_region_value;
static uint32_t pic_mask_calls;
static uint32_t pic_unmask_calls;
static uint32_t described_region_length = 0x4000U;
static uint32_t last_prepared_length;

pci_device_t pci_devices[2] = {
    {
        .vendor_id = 0x8086U, .device_id = 0x2668U,
        .bus = 0U, .slot = 27U, .function = 0U,
        .irq_line = 5U,
    },
};
size_t pci_device_count = 1U;

int register_interrupt_handler(int irq, void *handler) {
    return irq >= 0 && irq < PCI_LEGACY_IRQ_COUNT && handler != NULL ? 0 : -1;
}

int unregister_interrupt_handler(int irq, void *handler) {
    return irq >= 0 && irq < PCI_LEGACY_IRQ_COUNT && handler != NULL ? 0 : -1;
}

bool irq_pic_mask_line(uint8_t irq) {
    ++pic_mask_calls;
    return irq < PCI_LEGACY_IRQ_COUNT;
}

bool irq_pic_unmask_line(uint8_t irq) {
    ++pic_unmask_calls;
    return irq < PCI_LEGACY_IRQ_COUNT;
}

int ipc_capability_validate_owner(int owner_pid, uint32_t owner_generation,
        ipc_handle_t handle, uint32_t required_rights) {
    return owner_pid > 0 && owner_generation != 0U && handle == 11U &&
        required_rights == (IPC_RIGHT_RECEIVE | IPC_RIGHT_CONTROL) ? 0 : -13;
}

int ipc_send_kernel_to_owner(int owner_pid, uint32_t owner_generation,
        ipc_handle_t handle, const ipc_message_t *message) {
    if (owner_pid <= 0 || owner_generation == 0U || handle != 11U ||
        message == NULL || message->length != sizeof(last_notification))
        return -9;
    memcpy(&last_notification, message->payload, sizeof(last_notification));
    ++kernel_notifications;
    return 0;
}

static uint64_t monotonic_ms(void) {
    return fake_now_ms;
}

const pci_device_t *pci_find_location(uint32_t location) {
    for (size_t index = 0U; index < pci_device_count; ++index) {
        const pci_device_t *device = &pci_devices[index];
        uint32_t candidate = ((uint32_t)device->bus << 16U) |
            ((uint32_t)device->slot << 8U) | device->function;
        if (candidate == location) return device;
    }
    return NULL;
}

bool pci_irq_is_valid(uint8_t irq) {
    return irq < PCI_LEGACY_IRQ_COUNT;
}

bool pci_set_bus_master_verified(const pci_device_t *device, bool enabled) {
    (void)enabled;
    return device != NULL;
}

bool pci_set_intx_disabled_verified(const pci_device_t *device, bool disabled) {
    return device != NULL && disabled;
}

bool pci_function_reset_verified(const pci_device_t *device,
                                 uint64_t deadline_ms) {
    return device != NULL && deadline_ms > fake_now_ms;
}

bool pci_claim_for_driver_domain(uint32_t location, uint16_t vendor_id,
        uint16_t device_id, uint8_t class_code, uint8_t subclass_code,
        uint8_t prog_if) {
    return location != 0U && vendor_id == 0x8086U && device_id != 0U &&
        class_code == 0x04U && subclass_code == 0x03U && prog_if == 0U;
}

bool pci_describe_bar(const pci_device_t *device, uint32_t bar_index,
                      pci_bar_info_t *info) {
    (void)device;
    (void)bar_index;
    (void)info;
    return false;
}

static bool claim_device(uint32_t location,
                         const device_domain_profile_t *requested) {
    return requested != NULL && pci_claim_for_driver_domain(
        location, requested->vendor_id, requested->device_id,
        requested->class_code, requested->subclass_code, requested->prog_if);
}

static bool set_bus_master(uint32_t location, bool enabled) {
    assert(location != 0U);
    if (enabled) ++enable_calls;
    else ++disable_calls;
    return enabled ? bus_master_enable_result : bus_master_disable_result;
}

static bool mask_irq(uint32_t location) {
    assert(location != 0U);
    ++mask_calls;
    return mask_result;
}

static bool unmask_irq(uint32_t location) {
    assert(location != 0U);
    ++unmask_calls;
    return true;
}

static bool describe_region(uint32_t location, uint32_t region_index,
                            device_domain_region_info_t *region) {
    if (location == 0U || region == NULL || region_index != 0U) return false;
    *region = (device_domain_region_info_t){
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(*region),
        .region_index = region_index,
        .flags = DEVICE_DOMAIN_REGION_MMIO,
        .base_low = 0xFEBF0000U,
        .length_low = described_region_length,
    };
    return true;
}

static bool prepare_region(const device_domain_region_info_t *region) {
    if (region == NULL || region->length_low == 0U) return false;
    last_prepared_length = region->length_low;
    return true;
}

static bool read_region(const device_domain_region_info_t *region,
                        uint32_t offset, uint32_t width, uint32_t *value) {
    if (region == NULL || value == NULL || offset >= region->length_low ||
        (width != 1U && width != 2U && width != 4U)) return false;
    *value = 0xA5000000U | offset | width;
    return true;
}

static bool write_region(const device_domain_region_info_t *region,
                         uint32_t offset, uint32_t width, uint32_t value) {
    if (region == NULL || offset >= region->length_low ||
        (width != 1U && width != 2U && width != 4U)) return false;
    ++region_write_calls;
    last_region_value = value;
    return true;
}

static bool write_dma_address(const device_domain_region_info_t *region,
                              uint32_t offset, uint32_t address_low,
                              uint32_t address_high) {
    if (region == NULL || offset + 8U > region->length_low ||
        (address_low == 0U && address_high == 0U)) return false;
    ++dma_address_writes;
    return true;
}

static bool bind_irq(uint32_t location, int pid, uint32_t generation,
                     uint32_t capability) {
    return location != 0U && pid > 0 && generation != 0U && capability == 11U;
}

static bool revoke_irq(uint32_t location, int pid, uint32_t generation,
                       uint32_t capability) {
    assert(location != 0U && pid > 0 && generation != 0U && capability == 11U);
    ++irq_revoke_calls;
    return true;
}

static bool bind_dma(uint32_t location, int pid, uint32_t generation,
                     uint32_t mode, uint32_t capability) {
    return location != 0U && pid > 0 && generation != 0U &&
        ((mode == DEVICE_DOMAIN_MODE_MEDIATED && capability > 0U &&
          capability <= DEVICE_DOMAIN_DMA_POOL_COUNT) ||
         (mode == DEVICE_DOMAIN_MODE_IOMMU_DIRECT && capability == 12U));
}

static bool revoke_dma(uint32_t location, int pid, uint32_t generation,
                       uint32_t capability) {
    assert(location != 0U && pid > 0 && generation != 0U &&
           (capability == 12U ||
            (capability > 0U && capability <= DEVICE_DOMAIN_DMA_POOL_COUNT)));
    ++dma_revoke_calls;
    return true;
}

static bool reset_device(uint32_t location, uint64_t deadline_ms) {
    assert(location != 0U);
    assert(deadline_ms != 0U);
    ++reset_calls;
    return reset_result;
}

static device_domain_profile_t profile(uint32_t group, uint32_t flags) {
    return (device_domain_profile_t){
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(device_domain_profile_t),
        .isolation_group = group,
        .flags = flags,
        .vendor_id = 0x8086U,
        .device_id = 0x2668U,
        .class_code = 0x04U,
        .subclass_code = 0x03U,
    };
}

static int bind_irq_resource(int pid, uint32_t generation,
                             device_domain_handle_t device,
                             uint32_t capability,
                             device_domain_resource_handle_t *resource) {
    const device_domain_irq_request_t request = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(request),
        .device = device,
        .endpoint_capability = capability,
    };
    return device_domain_bind_irq(pid, generation, &request, resource);
}

static int bind_dma_resource(int pid, uint32_t generation,
                             device_domain_handle_t device,
                             uint32_t capability,
                             device_domain_resource_handle_t *resource) {
    const device_domain_dma_request_t request = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(request),
        .device = device,
        .dma_capability = capability,
        .flags = DEVICE_DOMAIN_DMA_TO_DEVICE |
                 DEVICE_DOMAIN_DMA_FROM_DEVICE,
    };
    return device_domain_bind_dma(pid, generation, &request, resource);
}

static void reset_counters(void) {
    device_domain_test_reset();
    mask_calls = 0U;
    disable_calls = 0U;
    enable_calls = 0U;
    unmask_calls = 0U;
    reset_calls = 0U;
    irq_revoke_calls = 0U;
    dma_revoke_calls = 0U;
    reset_result = true;
    mask_result = true;
    bus_master_enable_result = true;
    bus_master_disable_result = true;
    fake_now_ms = 10U;
    kernel_notifications = 0U;
    last_notification = (device_domain_irq_message_t){0};
    region_write_calls = 0U;
    dma_address_writes = 0U;
    last_region_value = 0U;
    pic_mask_calls = 0U;
    pic_unmask_calls = 0U;
    described_region_length = 0x4000U;
    last_prepared_length = 0U;
    pci_device_count = 1U;
    pci_devices[0].irq_line = 5U;
    pci_devices[1] = (pci_device_t){0};
}

static device_domain_platform_ops_t test_platform_ops(void) {
    return (device_domain_platform_ops_t){
        .monotonic_ms = monotonic_ms,
        .claim_device = claim_device,
        .set_bus_master = set_bus_master,
        .mask_irq = mask_irq,
        .unmask_irq = unmask_irq,
        .describe_region = describe_region,
        .prepare_region = prepare_region,
        .read_region = read_region,
        .write_region = write_region,
        .write_dma_address = write_dma_address,
        .bind_irq = bind_irq,
        .revoke_irq = revoke_irq,
        .bind_dma = bind_dma,
        .revoke_dma = revoke_dma,
        .reset = reset_device,
    };
}

static void activate_irq_test_device(
        int pid, uint32_t generation, uint32_t *device_out,
        device_domain_handle_t *handle_out,
        device_domain_resource_handle_t *irq_out) {
    device_domain_platform_ops_t ops = test_platform_ops();
    assert(device_domain_init(&ops, false));
    device_domain_profile_t device_profile = profile(
        7U, DEVICE_DOMAIN_PROFILE_MEDIATED_DMA);
    assert(device_domain_register(
        &device_profile, 0x00001B00U, device_out) == 0);
    assert(device_domain_claim(pid, generation, *device_out,
        DEVICE_DOMAIN_MODE_MEDIATED, handle_out) == 0);
    device_domain_resource_handle_t dma_resource = 0U;
    assert(bind_irq_resource(
        pid, generation, *handle_out, 11U, irq_out) == 0);
    assert(bind_dma_resource(
        pid, generation, *handle_out, 0U, &dma_resource) == 0);
    assert(device_domain_activate(pid, generation, *handle_out) == 0);
}

static void test_irq_storm_and_clock_regression_are_fenced(void) {
    reset_counters();
    uint32_t device = 0U;
    device_domain_handle_t handle = DEVICE_DOMAIN_INVALID_HANDLE;
    device_domain_resource_handle_t irq_resource = 0U;
    activate_irq_test_device(31, 17U, &device, &handle, &irq_resource);

    for (uint32_t irq = 0U; irq < DEVICE_DOMAIN_IRQ_WINDOW_LIMIT; ++irq) {
        device_domain_test_raise_irq(5U);
        device_domain_poll(100U);
        assert(kernel_notifications == irq + 1U);
        device_domain_irq_completion_t completion;
        assert(device_domain_irq_complete(
            31, 17U, irq_resource, &completion) == 0);
        assert(completion.completed_count == 1U);
    }
    device_domain_test_raise_irq(5U);
    device_domain_poll(100U);
    device_domain_status_t status;
    assert(device_domain_status(device, &status) == 0);
    assert(status.state == DEVICE_DOMAIN_FENCED);
    assert(kernel_notifications == DEVICE_DOMAIN_IRQ_WINDOW_LIMIT);
    assert(irq_revoke_calls == 1U && dma_revoke_calls == 1U);
    device_domain_dma_pool_stats_t pool_stats;
    assert(device_domain_dma_pool_stats(&pool_stats) == 0);
    assert(pool_stats.active_pools == 0U);

    reset_counters();
    activate_irq_test_device(32, 18U, &device, &handle, &irq_resource);
    device_domain_test_raise_irq(5U);
    device_domain_poll(200U);
    device_domain_irq_completion_t completion;
    assert(device_domain_irq_complete(
        32, 18U, irq_resource, &completion) == 0);
    device_domain_test_raise_irq(5U);
    device_domain_poll(199U);
    assert(device_domain_status(device, &status) == 0);
    assert(status.state == DEVICE_DOMAIN_FENCED);
    assert(kernel_notifications == 1U);
}

static void test_legacy_pic_fallback_masks_shared_irq(void) {
    reset_counters();
    mask_result = false;
    device_domain_platform_ops_t ops = test_platform_ops();
    assert(device_domain_init(&ops, false));
    device_domain_profile_t hda = profile(
        7U, DEVICE_DOMAIN_PROFILE_MEDIATED_DMA |
            DEVICE_DOMAIN_PROFILE_LEGACY_INTX_PIC);
    uint32_t device = UINT32_MAX;
    assert(device_domain_register(&hda, 0x00001B00U, &device) == 0);
    assert(device == 0U && pic_mask_calls == 1U);

    const device_domain_region_policy_t policy = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(policy),
        .readable_bytes = {0x100U},
        .rule_count = 1U,
        .rules = {{0U, 0x08U, 4U, DEVICE_DOMAIN_REGION_RULE_VALUE,
                   0x03U, 0U}},
    };
    assert(device_domain_install_region_policy(device, &policy) == 0);
    device_domain_handle_t handle = DEVICE_DOMAIN_INVALID_HANDLE;
    assert(device_domain_claim(17, 11U, device,
        DEVICE_DOMAIN_MODE_MEDIATED, &handle) == 0);
    device_domain_resource_handle_t irq_resource = 0U;
    device_domain_resource_handle_t dma_resource = 0U;
    assert(bind_irq_resource(17, 11U, handle, 11U, &irq_resource) == 0);
    assert(bind_dma_resource(17, 11U, handle, 0U, &dma_resource) == 0);
    assert(device_domain_activate(17, 11U, handle) == 0);
    assert(pic_unmask_calls == 1U);
    device_domain_test_raise_irq(5U);
    device_domain_poll(20U);
    assert(kernel_notifications == 1U);
    assert(pic_mask_calls == 2U && pic_unmask_calls == 1U);
    device_domain_irq_completion_t completion;
    assert(device_domain_irq_complete(
        17, 11U, irq_resource, &completion) == 0);
    assert(pic_unmask_calls == 2U);
    assert(device_domain_deactivate(17, 11U, handle) == 0);
    assert(pic_mask_calls == 3U);

    reset_counters();
    mask_result = false;
    pci_devices[1] = pci_devices[0];
    pci_devices[1].slot = 26U;
    pci_device_count = 2U;
    ops = test_platform_ops();
    assert(device_domain_init(&ops, false));
    device = UINT32_MAX;
    assert(device_domain_register(&hda, 0x00001B00U, &device) == 0);
    assert(device == 0U && pic_mask_calls == 1U);
}

static void test_mediated_lifecycle_and_stale_handle(void) {
    reset_counters();
    device_domain_platform_ops_t ops = {
        .monotonic_ms = monotonic_ms,
        .claim_device = claim_device,
        .set_bus_master = set_bus_master,
        .mask_irq = mask_irq,
        .unmask_irq = unmask_irq,
        .describe_region = describe_region,
        .prepare_region = prepare_region,
        .read_region = read_region,
        .write_region = write_region,
        .write_dma_address = write_dma_address,
        .bind_irq = bind_irq,
        .revoke_irq = revoke_irq,
        .bind_dma = bind_dma,
        .revoke_dma = revoke_dma,
        .reset = reset_device,
    };
    assert(device_domain_init(&ops, false));
    device_domain_iommu_status_t iommu;
    assert(device_domain_iommu_status(&iommu) == 0);
    assert(iommu.translation_enabled == 0U);
    assert(iommu.direct_assignment_ready == 0U);
    device_domain_profile_t hda = profile(
        1U, DEVICE_DOMAIN_PROFILE_MEDIATED_DMA);
    uint32_t device = UINT32_MAX;
    assert(device_domain_register(&hda, 0x00001B00U, &device) == 0);
    assert(device == 0U);
    assert(mask_calls == 1U && disable_calls == 1U && enable_calls == 0U);

    const device_domain_region_policy_t policy = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(policy),
        .readable_bytes = {0x100U},
        .rule_count = 3U,
        .rules = {
            {
                .region_index = 0U,
                .offset = 0x08U,
                .width = 4U,
                .kind = DEVICE_DOMAIN_REGION_RULE_VALUE,
                .writable_mask = 0x03U,
            },
            {
                .region_index = 0U,
                .offset = 0x18U,
                .width = 8U,
                .kind = DEVICE_DOMAIN_REGION_RULE_DMA_ADDRESS,
            },
            {
                .region_index = 0U,
                .offset = 0x28U,
                .width = 8U,
                .kind = DEVICE_DOMAIN_REGION_RULE_DMA_DESCRIPTOR_ADDRESS,
            },
        },
    };
    assert(device_domain_install_region_policy(device, &policy) == 0);
    assert(device_domain_install_region_policy(device, &policy) == -16);

    device_domain_handle_t handle = DEVICE_DOMAIN_INVALID_HANDLE;
    assert(device_domain_claim(7, 3U, device,
        DEVICE_DOMAIN_MODE_IOMMU_DIRECT, &handle) == -95);
    assert(device_domain_claim(7, 3U, device,
        DEVICE_DOMAIN_MODE_MEDIATED, &handle) == 0);
    assert(handle != DEVICE_DOMAIN_INVALID_HANDLE);
    assert(device_domain_activate(7, 3U, handle) == -22);
    assert(enable_calls == 0U);
    device_domain_region_request_t region_request = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(region_request),
        .device = handle,
        .region_index = 0U,
        .rights = DEVICE_DOMAIN_REGION_DESCRIBE |
                  DEVICE_DOMAIN_REGION_ACCESS_READ |
                  DEVICE_DOMAIN_REGION_ACCESS_WRITE,
    };
    device_domain_region_info_t region;
    assert(device_domain_open_region(7, 3U, &region_request, &region) == 0);
    assert(region.resource != DEVICE_DOMAIN_INVALID_HANDLE);
    assert(region.length_low == 0x100U);
    const device_domain_region_access_t read_request = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(read_request),
        .region = region.resource,
        .offset = 4U,
        .width = 4U,
    };
    device_domain_region_value_t read_result;
    assert(device_domain_region_read(
        7, 3U, &read_request, &read_result) == 0);
    assert(read_result.value == (0xA5000000U | 4U));
    device_domain_region_access_t write_request = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(write_request),
        .region = region.resource,
        .offset = 0x08U,
        .width = 4U,
        .value = 0x03U,
    };
    assert(device_domain_region_write(7, 3U, &write_request) == 0);
    assert(region_write_calls == 1U && last_region_value == 0x03U);
    write_request.value = 0x04U;
    assert(device_domain_region_write(7, 3U, &write_request) == -13);
    assert(region_write_calls == 1U);
    assert(device_domain_open_region(
        7, 3U, &region_request, &region) == -17);
    region_request.region_index = 1U;
    region_request.rights = DEVICE_DOMAIN_REGION_MAP_READ;
    assert(device_domain_open_region(
        7, 3U, &region_request, &region) == -95);
    device_domain_resource_handle_t irq_resource = 0U;
    device_domain_resource_handle_t dma_resource = 0U;
    assert(bind_irq_resource(7, 3U, handle, 99U, &irq_resource) == -13);
    assert(bind_irq_resource(7, 3U, handle, 11U, &irq_resource) == 0);
    assert(bind_dma_resource(7, 3U, handle, 99U, &dma_resource) == -22);
    assert(bind_dma_resource(7, 3U, handle, 0U, &dma_resource) == 0);
    device_domain_region_dma_address_t dma_address = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(dma_address),
        .region = region.resource,
        .dma = dma_resource,
        .register_offset = 0x18U,
        .buffer_offset = DEVICE_DOMAIN_DMA_DATA_OFFSET,
    };
    assert(device_domain_region_bind_dma(7, 3U, &dma_address) == 0);
    assert(dma_address_writes == 1U);
    dma_address.register_offset = 0x28U;
    dma_address.buffer_offset = 0U;
    assert(device_domain_region_bind_dma(7, 3U, &dma_address) == 0);
    assert(dma_address_writes == 2U);
    device_domain_dma_info_t dma_info;
    assert(device_domain_dma_info(7, 3U, dma_resource, &dma_info) == 0);
    assert(dma_info.capacity == DEVICE_DOMAIN_DMA_POOL_BYTES);
    assert(dma_info.alignment == 4096U);
    assert(dma_info.direction == (DEVICE_DOMAIN_DMA_TO_DEVICE |
                                  DEVICE_DOMAIN_DMA_FROM_DEVICE));
    assert(dma_info.reserved[0] == 0U && dma_info.reserved[1] == 0U);
    const uint8_t dma_input[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
    uint8_t dma_output[8] = {0};
    assert(device_domain_dma_write(
        7, 3U, dma_resource, 0U, dma_input, sizeof(dma_input)) == -22);
    assert(device_domain_dma_read(
        7, 3U, dma_resource, 0U, dma_output, sizeof(dma_output)) == -22);
    assert(device_domain_dma_write(
        7, 3U, dma_resource, DEVICE_DOMAIN_DMA_DATA_OFFSET + 32U,
        dma_input, sizeof(dma_input)) == 0);
    assert(device_domain_dma_read(
        7, 3U, dma_resource, DEVICE_DOMAIN_DMA_DATA_OFFSET + 32U,
        dma_output, sizeof(dma_output)) == 0);
    assert(memcmp(dma_input, dma_output, sizeof(dma_input)) == 0);
    const device_domain_dma_descriptor_t descriptor = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(descriptor),
        .dma = dma_resource,
        .descriptor_index = 0U,
        .buffer_offset = DEVICE_DOMAIN_DMA_DATA_OFFSET,
        .length = 128U,
        .flags = DEVICE_DOMAIN_DMA_DESCRIPTOR_INTERRUPT,
    };
    assert(device_domain_dma_descriptor_set(
        7, 3U, &descriptor) == 0);
    device_domain_resource_status_t resource_status;
    assert(device_domain_resource_status(
        7, 3U, irq_resource, &resource_status) == 0);
    assert(resource_status.kind == DEVICE_DOMAIN_RESOURCE_IRQ);
    assert(device_domain_resource_status(
        7, 3U, dma_resource, &resource_status) == 0);
    assert(resource_status.kind == DEVICE_DOMAIN_RESOURCE_DMA);
    device_domain_status_t status;
    assert(device_domain_activate(7, 3U, handle) == 0);
    assert(enable_calls == 1U);
    assert(unmask_calls == 1U);
    assert(device_domain_dma_write(
        7, 3U, dma_resource, DEVICE_DOMAIN_DMA_DATA_OFFSET,
        dma_input, sizeof(dma_input)) == -16);
    assert(device_domain_dma_descriptor_set(7, 3U, &descriptor) == -16);
    assert(device_domain_deactivate(7, 3U, handle) == 0);
    assert(mask_calls == 2U && disable_calls == 2U);
    assert(device_domain_status(device, &status) == 0);
    assert(status.state == DEVICE_DOMAIN_DMA_BOUND);
    assert(device_domain_dma_write(
        7, 3U, dma_resource, DEVICE_DOMAIN_DMA_DATA_OFFSET,
        dma_input, sizeof(dma_input)) == 0);
    assert(device_domain_dma_descriptor_set(7, 3U, &descriptor) == 0);
    assert(device_domain_deactivate(7, 3U, handle) == 0);
    assert(device_domain_activate(7, 3U, handle) == 0);
    assert(enable_calls == 2U && unmask_calls == 2U);

    device_domain_test_raise_irq(5U);
    device_domain_poll(20U);
    assert(kernel_notifications == 1U);
    assert(last_notification.resource == irq_resource);
    assert(last_notification.sequence == 1U);
    assert(last_notification.pending_count == 1U);
    device_domain_irq_completion_t completion;
    assert(device_domain_irq_complete(
        7, 3U, irq_resource, &completion) == 0);
    assert(completion.resource == irq_resource);
    assert(completion.sequence == 1U && completion.completed_count == 1U);
    assert(unmask_calls == 3U);

    assert(device_domain_status(device, &status) == 0);
    assert(status.state == DEVICE_DOMAIN_ACTIVE);
    assert(status.owner_pid == 7 && status.owner_generation == 3U);
    assert(status.irq_bound == 1U && status.dma_bound == 1U);

    assert(device_domain_release(7, 3U, handle, 100U) == 0);
    assert(reset_calls == 1U);
    assert(irq_revoke_calls == 1U && dma_revoke_calls == 1U);
    assert(device_domain_status(device, &status) == 0);
    assert(status.state == DEVICE_DOMAIN_AVAILABLE);
    assert(status.owner_pid == 0 && status.generation == 2U);
    assert(bind_irq_resource(7, 3U, handle, 11U, &irq_resource) == -9);
    assert(device_domain_resource_status(
        7, 3U, irq_resource, &resource_status) == -9);
    assert(device_domain_irq_complete(
        7, 3U, irq_resource, &completion) == -9);
    assert(device_domain_resource_status(
        7, 3U, region.resource, &resource_status) == -9);
    assert(device_domain_dma_info(7, 3U, dma_resource, &dma_info) == -9);
}

static void test_large_bar_is_clipped_to_read_only_policy_aperture(void) {
    reset_counters();
    described_region_length = 16U * 1024U * 1024U;
    device_domain_platform_ops_t ops = test_platform_ops();
    assert(device_domain_init(&ops, false));
    device_domain_profile_t video = profile(
        3U, DEVICE_DOMAIN_PROFILE_MEDIATED_IO);
    uint32_t device = UINT32_MAX;
    assert(device_domain_register(&video, 0x00001B00U, &device) == 0);
    const uint32_t aperture = 0x00400104U;
    const device_domain_region_policy_t policy = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(policy),
        .readable_bytes = {aperture},
    };
    assert(device_domain_install_region_policy(device, &policy) == 0);
    assert(last_prepared_length == aperture);

    device_domain_handle_t handle = DEVICE_DOMAIN_INVALID_HANDLE;
    assert(device_domain_claim(
        44, 9U, device, DEVICE_DOMAIN_MODE_MEDIATED, &handle) == 0);
    device_domain_resource_handle_t dma = DEVICE_DOMAIN_INVALID_HANDLE;
    assert(bind_dma_resource(44, 9U, handle, 0U, &dma) == -95);
    assert(dma == DEVICE_DOMAIN_INVALID_HANDLE);
    device_domain_dma_pool_stats_t stats;
    assert(device_domain_dma_pool_stats(&stats) == 0);
    assert(stats.active_pools == 0U && stats.capacity_rejections == 0U);
    const device_domain_region_request_t open_request = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(open_request),
        .device = handle,
        .region_index = 0U,
        .rights = DEVICE_DOMAIN_REGION_DESCRIBE |
                  DEVICE_DOMAIN_REGION_ACCESS_READ,
    };
    device_domain_region_info_t region;
    assert(device_domain_open_region(
        44, 9U, &open_request, &region) == 0);
    assert(region.length_low == aperture && region.length_high == 0U);
    assert(last_prepared_length == aperture);
    const device_domain_region_access_t last_word = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(last_word),
        .region = region.resource,
        .offset = aperture - sizeof(uint32_t),
        .width = sizeof(uint32_t),
    };
    device_domain_region_value_t value;
    assert(device_domain_region_read(44, 9U, &last_word, &value) == 0);
    device_domain_region_access_t outside = last_word;
    outside.offset = aperture;
    assert(device_domain_region_read(44, 9U, &outside, &value) == -22);

    reset_counters();
    described_region_length = 16U * 1024U * 1024U;
    ops = test_platform_ops();
    assert(device_domain_init(&ops, false));
    device = UINT32_MAX;
    assert(device_domain_register(&video, 0x00001B00U, &device) == 0);
    device_domain_region_policy_t oversized = policy;
    oversized.readable_bytes[0] = DEVICE_DOMAIN_MAX_REGION_BYTES + 4U;
    assert(device_domain_install_region_policy(device, &oversized) == -95);
    assert(last_prepared_length == 0U);
}

static void test_passive_mediated_dma_staging_is_quiesceable(void) {
    reset_counters();
    device_domain_platform_ops_t ops = test_platform_ops();
    assert(device_domain_init(&ops, false));
    device_domain_profile_t video = profile(
        3U, DEVICE_DOMAIN_PROFILE_MEDIATED_IO |
            DEVICE_DOMAIN_PROFILE_MEDIATED_DMA);
    uint32_t device = UINT32_MAX;
    assert(device_domain_register(&video, 0x00001B00U, &device) == 0);
    device_domain_handle_t handle = DEVICE_DOMAIN_INVALID_HANDLE;
    assert(device_domain_claim(
        44, 9U, device, DEVICE_DOMAIN_MODE_MEDIATED, &handle) == 0);
    device_domain_resource_handle_t dma = DEVICE_DOMAIN_INVALID_HANDLE;
    assert(bind_dma_resource(44, 9U, handle, 0U, &dma) == 0);
    assert(device_domain_mark_mediated_io_quiesced(44, 9U, handle) == 0);
    assert(device_domain_fence(44, 9U, handle) == 0);
    assert(mask_calls == 0U && disable_calls >= 2U);
    device_domain_dma_pool_stats_t stats;
    assert(device_domain_dma_pool_stats(&stats) == 0);
    assert(stats.active_pools == 0U);
}

static void test_group_exclusivity_and_failed_reset(void) {
    reset_counters();
    device_domain_platform_ops_t ops = {
        .monotonic_ms = monotonic_ms,
        .claim_device = claim_device,
        .set_bus_master = set_bus_master,
        .mask_irq = mask_irq,
        .unmask_irq = unmask_irq,
        .describe_region = describe_region,
        .prepare_region = prepare_region,
        .read_region = read_region,
        .write_region = write_region,
        .write_dma_address = write_dma_address,
        .bind_irq = bind_irq,
        .revoke_irq = revoke_irq,
        .bind_dma = bind_dma,
        .revoke_dma = revoke_dma,
        .reset = reset_device,
    };
    assert(device_domain_init(&ops, false));
    device_domain_profile_t first = profile(
        2U, DEVICE_DOMAIN_PROFILE_MEDIATED_DMA);
    device_domain_profile_t second = first;
    second.device_id = 0x2669U;
    uint32_t device_a = 0U;
    uint32_t device_b = 0U;
    assert(device_domain_register(&first, 0x00001B00U, &device_a) == 0);
    assert(device_domain_register(&second, 0x00001B01U, &device_b) == 0);
    device_domain_handle_t handle_a = 0U;
    device_domain_handle_t handle_b = 0U;
    assert(device_domain_claim(8, 4U, device_a,
        DEVICE_DOMAIN_MODE_MEDIATED, &handle_a) == 0);
    assert(device_domain_claim(9, 4U, device_b,
        DEVICE_DOMAIN_MODE_MEDIATED, &handle_b) == -16);
    assert(device_domain_claim(8, 4U, device_b,
        DEVICE_DOMAIN_MODE_MEDIATED, &handle_b) == 0);

    reset_result = false;
    assert(device_domain_release(8, 4U, handle_a, 100U) == -5);
    device_domain_status_t status;
    assert(device_domain_status(device_a, &status) == 0);
    assert(status.state == DEVICE_DOMAIN_FENCED);
    assert(status.owner_pid == 8);
    assert(device_domain_claim(9, 5U, device_a,
        DEVICE_DOMAIN_MODE_MEDIATED, &handle_a) == -16);

    reset_result = true;
    device_domain_process_cleanup(8, 4U);
    assert(device_domain_status(device_a, &status) == 0);
    assert(status.state == DEVICE_DOMAIN_FENCED);
    assert(device_domain_release(8, 4U, handle_a, 200U) == 0);
    assert(device_domain_status(device_b, &status) == 0);
    assert(status.state == DEVICE_DOMAIN_FENCED);
    assert(device_domain_release(8, 4U, handle_b, 200U) == 0);
}

static void test_direct_assignment_requires_both_proofs(void) {
    reset_counters();
    device_domain_platform_ops_t ops = {
        .monotonic_ms = monotonic_ms,
        .claim_device = claim_device,
        .set_bus_master = set_bus_master,
        .mask_irq = mask_irq,
        .unmask_irq = unmask_irq,
        .describe_region = describe_region,
        .prepare_region = prepare_region,
        .read_region = read_region,
        .write_region = write_region,
        .write_dma_address = write_dma_address,
        .bind_irq = bind_irq,
        .revoke_irq = revoke_irq,
        .bind_dma = bind_dma,
        .revoke_dma = revoke_dma,
        .reset = reset_device,
    };
    device_domain_profile_t direct = profile(
        3U, DEVICE_DOMAIN_PROFILE_IOMMU_DIRECT |
            DEVICE_DOMAIN_PROFILE_GROUP_ISOLATED);
    uint32_t device = 0U;
    device_domain_handle_t handle = 0U;

    assert(device_domain_init(&ops, false));
    assert(device_domain_register(&direct, 0x00000200U, &device) == 0);
    assert(device_domain_claim(10, 6U, device,
        DEVICE_DOMAIN_MODE_IOMMU_DIRECT, &handle) == -95);

    device_domain_test_reset();
    assert(device_domain_init(&ops, true));
    device_domain_iommu_status_t iommu;
    assert(device_domain_iommu_status(&iommu) == 0);
    assert(iommu.translation_enabled == 1U);
    assert(iommu.direct_assignment_ready == 1U);
    assert(device_domain_register(&direct, 0x00000200U, &device) == 0);
    assert(device_domain_claim(10, 6U, device,
        DEVICE_DOMAIN_MODE_IOMMU_DIRECT, &handle) == 0);
}

static void test_every_fence_action_runs_after_partial_failure(void) {
    reset_counters();
    device_domain_platform_ops_t ops = {
        .monotonic_ms = monotonic_ms,
        .claim_device = claim_device,
        .set_bus_master = set_bus_master,
        .mask_irq = mask_irq,
        .unmask_irq = unmask_irq,
        .describe_region = describe_region,
        .prepare_region = prepare_region,
        .read_region = read_region,
        .write_region = write_region,
        .write_dma_address = write_dma_address,
        .bind_irq = bind_irq,
        .revoke_irq = revoke_irq,
        .bind_dma = bind_dma,
        .revoke_dma = revoke_dma,
        .reset = reset_device,
    };
    assert(device_domain_init(&ops, false));
    device_domain_profile_t hda = profile(
        4U, DEVICE_DOMAIN_PROFILE_MEDIATED_DMA);
    uint32_t device = 0U;
    assert(device_domain_register(&hda, 0x00001B00U, &device) == 0);
    device_domain_handle_t handle = 0U;
    assert(device_domain_claim(11, 7U, device,
        DEVICE_DOMAIN_MODE_MEDIATED, &handle) == 0);
    device_domain_resource_handle_t irq_resource = 0U;
    device_domain_resource_handle_t dma_resource = 0U;
    assert(bind_irq_resource(11, 7U, handle, 11U, &irq_resource) == 0);
    assert(bind_dma_resource(11, 7U, handle, 0U, &dma_resource) == 0);

    mask_result = false;
    bus_master_disable_result = false;
    assert(device_domain_fence(11, 7U, handle) == -5);
    assert(mask_calls == 2U);
    assert(disable_calls == 2U);
    assert(irq_revoke_calls == 1U);
    assert(dma_revoke_calls == 1U);
    device_domain_status_t status;
    assert(device_domain_status(device, &status) == 0);
    assert(status.state == DEVICE_DOMAIN_FENCED);
    assert(status.irq_bound == 0U && status.dma_bound == 0U);
}

static void test_registration_attempts_both_initial_fences(void) {
    reset_counters();
    device_domain_platform_ops_t ops = {
        .monotonic_ms = monotonic_ms,
        .claim_device = claim_device,
        .set_bus_master = set_bus_master,
        .mask_irq = mask_irq,
        .unmask_irq = unmask_irq,
        .describe_region = describe_region,
        .prepare_region = prepare_region,
        .read_region = read_region,
        .write_region = write_region,
        .write_dma_address = write_dma_address,
        .bind_irq = bind_irq,
        .revoke_irq = revoke_irq,
        .bind_dma = bind_dma,
        .revoke_dma = revoke_dma,
        .reset = reset_device,
    };
    assert(device_domain_init(&ops, false));
    mask_result = false;
    device_domain_profile_t hda = profile(
        5U, DEVICE_DOMAIN_PROFILE_MEDIATED_DMA);
    uint32_t device = 0U;
    assert(device_domain_register(&hda, 0x00001B00U, &device) == -5);
    assert(mask_calls == 1U && disable_calls == 1U);
    device_domain_status_t status;
    assert(device_domain_status(device, &status) == 0);
    assert(status.state == DEVICE_DOMAIN_UNSUPPORTED);
}

static void test_owner_recovery_is_atomic_and_deadline_bounded(void) {
    reset_counters();
    device_domain_platform_ops_t ops = {
        .monotonic_ms = monotonic_ms,
        .claim_device = claim_device,
        .set_bus_master = set_bus_master,
        .mask_irq = mask_irq,
        .unmask_irq = unmask_irq,
        .describe_region = describe_region,
        .prepare_region = prepare_region,
        .read_region = read_region,
        .write_region = write_region,
        .write_dma_address = write_dma_address,
        .bind_irq = bind_irq,
        .revoke_irq = revoke_irq,
        .bind_dma = bind_dma,
        .revoke_dma = revoke_dma,
        .reset = reset_device,
    };
    assert(device_domain_init(&ops, false));
    device_domain_profile_t first = profile(
        6U, DEVICE_DOMAIN_PROFILE_MEDIATED_DMA);
    device_domain_profile_t second = first;
    second.device_id = 0x2669U;
    uint32_t device_a = 0U;
    uint32_t device_b = 0U;
    assert(device_domain_register(&first, 0x00001B00U, &device_a) == 0);
    assert(device_domain_register(&second, 0x00001B01U, &device_b) == 0);
    device_domain_handle_t handle_a = 0U;
    device_domain_handle_t handle_b = 0U;
    assert(device_domain_claim(12, 8U, device_a,
        DEVICE_DOMAIN_MODE_MEDIATED, &handle_a) == 0);
    assert(device_domain_claim(12, 8U, device_b,
        DEVICE_DOMAIN_MODE_MEDIATED, &handle_b) == 0);

    reset_result = false;
    assert(device_domain_recover_owner(12, 8U, 100U) == -5);
    device_domain_status_t status;
    assert(device_domain_status(device_a, &status) == 0);
    assert(status.state == DEVICE_DOMAIN_FENCED && status.owner_pid == 12);
    assert(device_domain_status(device_b, &status) == 0);
    assert(status.state == DEVICE_DOMAIN_FENCED && status.owner_pid == 12);

    reset_result = true;
    assert(device_domain_recover_owner(12, 8U, 100U) == 0);
    assert(device_domain_status(device_a, &status) == 0);
    assert(status.state == DEVICE_DOMAIN_AVAILABLE && status.generation == 2U);
    assert(device_domain_status(device_b, &status) == 0);
    assert(status.state == DEVICE_DOMAIN_AVAILABLE && status.generation == 2U);
    device_domain_resource_handle_t stale_resource = 0U;
    assert(bind_irq_resource(
        12, 8U, handle_a, 11U, &stale_resource) == -9);
    assert(bind_irq_resource(
        12, 8U, handle_b, 11U, &stale_resource) == -9);

    assert(device_domain_claim(13, 9U, device_a,
        DEVICE_DOMAIN_MODE_MEDIATED, &handle_a) == 0);
    fake_now_ms = 100U;
    assert(device_domain_recover_owner(13, 9U, 100U) == -110);
    assert(device_domain_release(13, 9U, handle_a, 100U) == -110);
    assert(device_domain_status(device_a, &status) == 0);
    assert(status.owner_pid == 13 && status.state == DEVICE_DOMAIN_CLAIMED);
}

static void test_dma_pool_pressure_is_saturating_and_reclaimed(void) {
    reset_counters();
    device_domain_platform_ops_t ops = test_platform_ops();
    assert(device_domain_init(&ops, false));

    device_domain_dma_pool_stats_t stats;
    assert(device_domain_dma_pool_stats(&stats) == 0);
    assert(stats.version == DEVICE_DOMAIN_ABI_VERSION);
    assert(stats.struct_size == sizeof(stats));
    assert(stats.active_pools == 0U && stats.peak_active_pools == 0U);
    assert(stats.capacity == DEVICE_DOMAIN_DMA_POOL_COUNT);
    assert(stats.capacity_rejections == 0U);
    assert(stats.pool_bytes == DEVICE_DOMAIN_DMA_POOL_BYTES);
    assert(stats.reserved == 0U);

    uint32_t devices[DEVICE_DOMAIN_DMA_POOL_COUNT + 1U] = {0};
    device_domain_handle_t handles[DEVICE_DOMAIN_DMA_POOL_COUNT + 1U] = {0};
    device_domain_resource_handle_t dma[DEVICE_DOMAIN_DMA_POOL_COUNT + 1U] =
        {0};
    for (uint32_t index = 0U; index <= DEVICE_DOMAIN_DMA_POOL_COUNT; ++index) {
        device_domain_profile_t current = profile(
            index + 1U, DEVICE_DOMAIN_PROFILE_MEDIATED_DMA);
        current.device_id = (uint16_t)(0x2700U + index);
        assert(device_domain_register(
            &current, 0x00010000U + index, &devices[index]) == 0);
        assert(device_domain_claim(
            20 + (int)index, 10U + index, devices[index],
            DEVICE_DOMAIN_MODE_MEDIATED, &handles[index]) == 0);
        if (index < DEVICE_DOMAIN_DMA_POOL_COUNT) {
            assert(bind_dma_resource(
                20 + (int)index, 10U + index, handles[index], 0U,
                &dma[index]) == 0);
        }
    }

    assert(device_domain_dma_pool_stats(&stats) == 0);
    assert(stats.active_pools == DEVICE_DOMAIN_DMA_POOL_COUNT);
    assert(stats.peak_active_pools == DEVICE_DOMAIN_DMA_POOL_COUNT);
    assert(stats.capacity_rejections == 0U);

    assert(bind_dma_resource(
        20 + (int)DEVICE_DOMAIN_DMA_POOL_COUNT,
        10U + DEVICE_DOMAIN_DMA_POOL_COUNT,
        handles[DEVICE_DOMAIN_DMA_POOL_COUNT], 1U,
        &dma[DEVICE_DOMAIN_DMA_POOL_COUNT]) == -22);
    assert(device_domain_dma_pool_stats(&stats) == 0);
    assert(stats.capacity_rejections == 0U);
    assert(bind_dma_resource(
        20 + (int)DEVICE_DOMAIN_DMA_POOL_COUNT,
        10U + DEVICE_DOMAIN_DMA_POOL_COUNT,
        handles[DEVICE_DOMAIN_DMA_POOL_COUNT], 0U,
        &dma[DEVICE_DOMAIN_DMA_POOL_COUNT]) == -28);
    assert(device_domain_dma_pool_stats(&stats) == 0);
    assert(stats.active_pools == DEVICE_DOMAIN_DMA_POOL_COUNT);
    assert(stats.capacity_rejections == 1U);

    assert(device_domain_release(20, 10U, handles[0], 100U) == 0);
    assert(device_domain_dma_pool_stats(&stats) == 0);
    assert(stats.active_pools == DEVICE_DOMAIN_DMA_POOL_COUNT - 1U);
    assert(stats.peak_active_pools == DEVICE_DOMAIN_DMA_POOL_COUNT);
    assert(stats.capacity_rejections == 1U);
    assert(bind_dma_resource(
        20 + (int)DEVICE_DOMAIN_DMA_POOL_COUNT,
        10U + DEVICE_DOMAIN_DMA_POOL_COUNT,
        handles[DEVICE_DOMAIN_DMA_POOL_COUNT], 0U,
        &dma[DEVICE_DOMAIN_DMA_POOL_COUNT]) == 0);

    for (uint32_t index = 1U; index <= DEVICE_DOMAIN_DMA_POOL_COUNT; ++index)
        assert(device_domain_release(
            20 + (int)index, 10U + index, handles[index], 100U) == 0);
    assert(device_domain_dma_pool_stats(&stats) == 0);
    assert(stats.active_pools == 0U);
    assert(stats.peak_active_pools == DEVICE_DOMAIN_DMA_POOL_COUNT);
    assert(stats.capacity_rejections == 1U);
}

static void test_large_dma_pool_is_explicit_and_profile_scoped(void) {
    reset_counters();
    device_domain_platform_ops_t ops = test_platform_ops();
    assert(device_domain_init(&ops, false));

    device_domain_profile_t invalid = profile(
        1U, DEVICE_DOMAIN_PROFILE_LARGE_DMA_POOL);
    uint32_t unused = 0U;
    assert(device_domain_register(&invalid, 0x00012000U, &unused) == -22);

    device_domain_profile_t normal = profile(
        2U, DEVICE_DOMAIN_PROFILE_MEDIATED_DMA);
    device_domain_profile_t large = profile(
        3U, DEVICE_DOMAIN_PROFILE_MEDIATED_DMA |
            DEVICE_DOMAIN_PROFILE_LARGE_DMA_POOL);
    large.device_id = 0x2670U;
    uint32_t normal_device = 0U;
    uint32_t large_device = 0U;
    assert(device_domain_register(
        &normal, 0x00012001U, &normal_device) == 0);
    assert(device_domain_register(
        &large, 0x00012002U, &large_device) == 0);
    device_domain_handle_t normal_handle = 0U;
    device_domain_handle_t large_handle = 0U;
    assert(device_domain_claim(30, 20U, normal_device,
        DEVICE_DOMAIN_MODE_MEDIATED, &normal_handle) == 0);
    assert(device_domain_claim(31, 21U, large_device,
        DEVICE_DOMAIN_MODE_MEDIATED, &large_handle) == 0);
    device_domain_resource_handle_t normal_dma = 0U;
    device_domain_resource_handle_t large_dma = 0U;
    assert(bind_dma_resource(
        30, 20U, normal_handle, 0U, &normal_dma) == 0);
    assert(bind_dma_resource(
        31, 21U, large_handle, 0U, &large_dma) == 0);

    device_domain_dma_info_t info;
    assert(device_domain_dma_info(30, 20U, normal_dma, &info) == 0);
    assert(info.capacity == DEVICE_DOMAIN_DMA_POOL_BYTES);
    assert(device_domain_dma_info(31, 21U, large_dma, &info) == 0);
    assert(info.capacity == DEVICE_DOMAIN_DMA_LARGE_POOL_BYTES);

    const uint32_t offset = DEVICE_DOMAIN_DMA_LARGE_POOL_BYTES - 4U;
    uint32_t value = 0xA55A5AA5U;
    uint32_t readback = 0U;
    assert(device_domain_dma_write(
        30, 20U, normal_dma, offset, &value, sizeof(value)) == -22);
    assert(device_domain_dma_write(
        31, 21U, large_dma, offset, &value, sizeof(value)) == 0);
    assert(device_domain_dma_read(
        31, 21U, large_dma, offset, &readback, sizeof(readback)) == 0);
    assert(readback == value);

    device_domain_dma_descriptor_t descriptor = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(descriptor),
        .dma = large_dma,
        .descriptor_index = 0U,
        .buffer_offset = DEVICE_DOMAIN_DMA_LARGE_POOL_BYTES - 128U,
        .length = 128U,
    };
    assert(device_domain_dma_descriptor_set(31, 21U, &descriptor) == 0);
    descriptor.dma = normal_dma;
    assert(device_domain_dma_descriptor_set(30, 20U, &descriptor) == -22);

    assert(device_domain_release(30, 20U, normal_handle, 100U) == 0);
    assert(device_domain_release(31, 21U, large_handle, 100U) == 0);
}

static void test_dma_relocation_seal_is_atomic_and_generation_scoped(void) {
    reset_counters();
    device_domain_platform_ops_t ops = test_platform_ops();
    assert(device_domain_init(&ops, false));
    device_domain_profile_t current = profile(
        4U, DEVICE_DOMAIN_PROFILE_MEDIATED_DMA |
            DEVICE_DOMAIN_PROFILE_LARGE_DMA_POOL);
    uint32_t device = 0U;
    assert(device_domain_register(&current, 0x00013000U, &device) == 0);

    device_domain_dma_relocation_policy_t policy = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(policy),
        .policy_count = 1U,
        .policies = {{
            .policy_id = 17U,
            .rule_count = 2U,
            .rules = {
                {
                    .destination_pool_offset = 0x5008U,
                    .source_pool_offset = 0x4000U,
                    .width = sizeof(uint64_t),
                },
                {
                    .destination_pool_offset = 0x5200U,
                    .source_pool_offset = 0x10000U,
                    .width = sizeof(uint64_t),
                    .fixed_bits = 3U,
                },
            },
        }},
    };
    device_domain_dma_relocation_policy_t invalid_policy = policy;
    ++invalid_policy.policies[0].rules[0].source_pool_offset;
    assert(device_domain_install_dma_relocation_policy(
        device, &invalid_policy) == -22);
    assert(device_domain_install_dma_relocation_policy(
        device, &policy) == 0);
    assert(device_domain_install_dma_relocation_policy(
        device, &policy) == -16);

    device_domain_handle_t handle = 0U;
    assert(device_domain_claim(40, 30U, device,
        DEVICE_DOMAIN_MODE_MEDIATED, &handle) == 0);
    device_domain_resource_handle_t dma = 0U;
    assert(bind_dma_resource(40, 30U, handle, 0U, &dma) == 0);
    device_domain_dma_relocation_request_t request = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(request),
        .dma = dma,
        .policy_id = 17U,
        .rule_count = 2U,
    };
    request.rules[0] = policy.policies[0].rules[0];
    request.rules[1] = policy.policies[0].rules[1];

    device_domain_dma_relocation_request_t mutated = request;
    mutated.rules[1].fixed_bits = 7U;
    assert(device_domain_dma_relocate_and_seal(
        40, 30U, &mutated) == -13);
    uint64_t word = UINT64_MAX;
    assert(device_domain_test_dma_word(dma, 0x5008U, &word));
    assert(word == 0U);

    const uint64_t poison = 1U;
    assert(device_domain_dma_write(
        40, 30U, dma, 0x5200U, &poison, sizeof(poison)) == 0);
    assert(device_domain_dma_relocate_and_seal(
        40, 30U, &request) == -84);
    assert(device_domain_test_dma_word(dma, 0x5008U, &word));
    assert(word == 0U);
    const uint64_t zero = 0U;
    assert(device_domain_dma_write(
        40, 30U, dma, 0x5200U, &zero, sizeof(zero)) == 0);
    assert(device_domain_dma_relocate_and_seal(
        40, 30U, &request) == 0);
    assert(device_domain_test_dma_word(dma, 0x5008U, &word));
    assert(word == 0x10004000ULL);
    assert(device_domain_test_dma_word(dma, 0x5200U, &word));
    assert(word == 0x10010003ULL);
    assert(device_domain_dma_read(
        40, 30U, dma, 0x5008U, &word, sizeof(word)) == -16);
    assert(device_domain_dma_write(
        40, 30U, dma, 0x6000U, &zero, sizeof(zero)) == -16);
    assert(device_domain_dma_relocate_and_seal(
        40, 30U, &request) == -16);
    device_domain_dma_descriptor_t descriptor = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(descriptor),
        .dma = dma,
        .buffer_offset = 0x2000U,
        .length = 128U,
    };
    assert(device_domain_dma_descriptor_set(40, 30U, &descriptor) == -16);
    assert(device_domain_release(40, 30U, handle, 100U) == 0);

    assert(device_domain_claim(41, 31U, device,
        DEVICE_DOMAIN_MODE_MEDIATED, &handle) == 0);
    assert(bind_dma_resource(41, 31U, handle, 0U, &dma) == 0);
    word = UINT64_MAX;
    assert(device_domain_dma_read(
        41, 31U, dma, 0x5008U, &word, sizeof(word)) == 0);
    assert(word == 0U);
    assert(device_domain_release(41, 31U, handle, 100U) == 0);
}

int main(void) {
    test_irq_storm_and_clock_regression_are_fenced();
    test_legacy_pic_fallback_masks_shared_irq();
    test_mediated_lifecycle_and_stale_handle();
    test_large_bar_is_clipped_to_read_only_policy_aperture();
    test_passive_mediated_dma_staging_is_quiesceable();
    test_group_exclusivity_and_failed_reset();
    test_direct_assignment_requires_both_proofs();
    test_every_fence_action_runs_after_partial_failure();
    test_registration_attempts_both_initial_fences();
    test_owner_recovery_is_atomic_and_deadline_bounded();
    test_dma_pool_pressure_is_saturating_and_reclaimed();
    test_large_dma_pool_is_explicit_and_profile_scoped();
    test_dma_relocation_seal_is_atomic_and_generation_scoped();
    return 0;
}
