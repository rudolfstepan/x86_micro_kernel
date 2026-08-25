/** @file video_device_profile.h
 *  @brief Exact display-device profiles for bounded Ring-3 drivers. */
#ifndef REIST_KERNEL_VIDEO_DEVICE_PROFILE_H
#define REIST_KERNEL_VIDEO_DEVICE_PROFILE_H

#include <stdint.h>

typedef struct {
    uint32_t device_index;
    uint32_t pci_location;
    uint32_t backend;
    uint16_t vendor_id;
    uint16_t device_id;
} video_device_profile_info_t;

enum {
    VIDEO_DEVICE_BACKEND_NONE = 0U,
    VIDEO_DEVICE_BACKEND_VMWARE_SVGA2 = 1U,
    VIDEO_DEVICE_BACKEND_NVIDIA_GK208 = 2U,
};

int video_device_profile_discover(video_device_profile_info_t *info);

#endif
