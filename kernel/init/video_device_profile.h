/** @file video_device_profile.h
 *  @brief Exact VMware SVGA-II profile for the bounded Ring-3 driver. */
#ifndef REIST_KERNEL_VIDEO_DEVICE_PROFILE_H
#define REIST_KERNEL_VIDEO_DEVICE_PROFILE_H

#include <stdint.h>

typedef struct {
    uint32_t device_index;
    uint32_t pci_location;
    uint16_t vendor_id;
    uint16_t device_id;
} video_device_profile_info_t;

int video_device_profile_discover(video_device_profile_info_t *info);

#endif
