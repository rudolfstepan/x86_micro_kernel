/**
 * @file kernel/init/audio_device_profile.h
 * @brief Immutable Intel HDA safety-profile discovery for Ring-3 drivers.
 *
 * This interface identifies and bounds controller resources only. Controller,
 * codec and stream state machines are intentionally implemented in Ring 3.
 */
#ifndef REIST_KERNEL_AUDIO_DEVICE_PROFILE_H
#define REIST_KERNEL_AUDIO_DEVICE_PROFILE_H

#include <stdint.h>

typedef struct {
    uint32_t device_index;
    uint32_t pci_location;
    uint32_t output_stream_base;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t gcap;
    uint8_t input_streams;
    uint8_t output_streams;
    uint8_t codec_mask;
    uint8_t reserved[3];
} audio_device_profile_info_t;

/**
 * Discover and register one specification-compatible HDA controller.
 *
 * @return 1 when a profile was installed, 0 when no HDA function exists, or
 *         a negative errno-compatible value when present hardware is unsafe.
 */
int audio_device_profile_discover(audio_device_profile_info_t *info);

#endif
