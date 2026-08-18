/**
 * @file arch/x86/boot/vbe_runtime.h
 * @brief Validierter VBE-Laufzeit-Handoff und fest begrenzter Mode-Set-Thunk.
 */
#ifndef REIST_VBE_RUNTIME_H
#define REIST_VBE_RUNTIME_H

#include <stdint.h>

#define VBE_RUNTIME_INFO_ADDRESS 0x00008400U
#define VBE_RUNTIME_INFO_MAGIC   0x52454256U
#define VBE_RUNTIME_INFO_VERSION 1U

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t struct_size;
    uint16_t mode;
    uint16_t reserved;
    uint32_t framebuffer_address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t memory_type;
    uint8_t red_position;
    uint8_t red_size;
    uint8_t green_position;
    uint8_t green_size;
    uint8_t blue_position;
    uint8_t blue_size;
} vbe_runtime_info_t;

/* Accepts only the mode copied from the validated loader handoff. */
int vbe_runtime_set_mode(uint16_t mode);
int vbe_runtime_set_text_mode(void);

#endif
