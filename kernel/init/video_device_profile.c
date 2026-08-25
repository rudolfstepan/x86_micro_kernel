/**
 * @file video_device_profile.c
 * @brief Fail-closed VMware SVGA-II and NVIDIA GK208 ownership profiles.
 *
 * VMware device identifiers follow vm_device_version.h from open-vm-tools.
 * The profile deliberately grants no DMA, IRQ or raw BAR authority.  Ring 0
 * retains a small fixed-command FIFO mediator and validates every rectangle.
 */
#include "kernel/init/video_device_profile.h"

#include <stddef.h>

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

static int register_profile(const pci_device_t *device, uint32_t backend,
                            video_device_profile_info_t *info) {
    const device_domain_profile_t profile = {
        .version = DEVICE_DOMAIN_ABI_VERSION,
        .struct_size = sizeof(profile),
        .isolation_group = DEVICE_DOMAIN_MAX_GROUPS - 2U,
        .flags = DEVICE_DOMAIN_PROFILE_MEDIATED_IO,
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
