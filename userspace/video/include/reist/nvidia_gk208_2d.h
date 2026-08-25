/**
 * @file nvidia_gk208_2d.h
 * @brief Fixed FERMI_TWOD_A command contract for the exact GK208 target.
 *
 * This compiler does not submit work.  It produces one bounded, validated
 * method stream for a later kernel-owned GPFIFO mediator.
 */
#ifndef REIST_VIDEO_NVIDIA_GK208_2D_H
#define REIST_VIDEO_NVIDIA_GK208_2D_H

#include <stdint.h>

#define REIST_NVIDIA_GK208_FERMI_TWOD_A 0x0000902DU
#define REIST_NVIDIA_GK208_PUSHBUF_WORD_CAPACITY 64U
#define REIST_NVIDIA_GK208_2D_SUBCHANNEL 3U
#define REIST_NVIDIA_GK208_SURFACE_ALIGNMENT 256U
#define REIST_NVIDIA_GK208_MAX_DIMENSION 4096U
#define REIST_NVIDIA_GK208_MAX_PITCH 65536U

typedef struct {
    uint64_t gpu_address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
} reist_nvidia_gk208_surface_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} reist_nvidia_gk208_rect_t;

typedef struct {
    uint32_t words[REIST_NVIDIA_GK208_PUSHBUF_WORD_CAPACITY];
    uint32_t word_count;
} reist_nvidia_gk208_pushbuf_t;

int reist_nvidia_gk208_encode_fill(
    reist_nvidia_gk208_pushbuf_t *pushbuf,
    const reist_nvidia_gk208_surface_t *surface,
    const reist_nvidia_gk208_rect_t *destination,
    uint32_t xrgb8888);
int reist_nvidia_gk208_encode_copy(
    reist_nvidia_gk208_pushbuf_t *pushbuf,
    const reist_nvidia_gk208_surface_t *surface,
    const reist_nvidia_gk208_rect_t *source,
    const reist_nvidia_gk208_rect_t *destination);
int reist_nvidia_gk208_validate_pushbuf(
    const reist_nvidia_gk208_pushbuf_t *pushbuf);
int reist_nvidia_gk208_command_self_test(void);

#endif
