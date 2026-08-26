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
#define REIST_NVIDIA_GK208_SUBMISSION_WORD_CAPACITY 72U
#define REIST_NVIDIA_GK208_GPFIFO_ENTRY_WORDS 2U
#define REIST_NVIDIA_GK208_2D_SUBCHANNEL 3U
#define REIST_NVIDIA_GK208_SURFACE_ALIGNMENT 256U
#define REIST_NVIDIA_GK208_MAX_DIMENSION 4096U
#define REIST_NVIDIA_GK208_MAX_PITCH 65536U
#define REIST_NVIDIA_GK208_DMA_POOL_BYTES (64U * 1024U)
#define REIST_NVIDIA_GK208_DMA_DESCRIPTOR_BYTES 4096U
#define REIST_NVIDIA_GK208_DMA_GPFIFO_OFFSET 0x00001000U
#define REIST_NVIDIA_GK208_DMA_PUSHBUF_OFFSET 0x00002000U
#define REIST_NVIDIA_GK208_DMA_FENCE_OFFSET 0x00003000U
#define REIST_NVIDIA_GK208_DMA_USERD_OFFSET 0x00004000U
#define REIST_NVIDIA_GK208_DMA_RAMFC_OFFSET 0x00005000U
#define REIST_NVIDIA_GK208_DMA_RUNLIST_OFFSET 0x00006000U
#define REIST_NVIDIA_GK208_PUSHBUF_GPU_ADDRESS 0x0000000020000000ULL
#define REIST_NVIDIA_GK208_FENCE_GPU_ADDRESS 0x0000000020001000ULL
#define REIST_NVIDIA_GK208_GPFIFO_GPU_ADDRESS 0x0000000020002000ULL
#define REIST_NVIDIA_GK208_GPFIFO_BYTES 0x00001000U
#define REIST_NVIDIA_GK208_RAMFC_BYTES 0x00001000U
#define REIST_NVIDIA_GK208_USERD_BYTES 0x00000200U
#define REIST_NVIDIA_GK208_RUNLIST_BYTES 8U
#define REIST_NVIDIA_GK208_CHANNEL_ID 1U
#define REIST_NVIDIA_GK208_CHANNEL_LIMIT 1024U
#define REIST_NVIDIA_GK208_GR_DEVICE_MASK 1U
#define REIST_NVIDIA_GK208_ADDRESS_RELOCATION_WIDTH 8U
#define REIST_NVIDIA_GK208_RAMFC_WORDS \
    (REIST_NVIDIA_GK208_RAMFC_BYTES / sizeof(uint32_t))
#define REIST_NVIDIA_GK208_USERD_WORDS \
    (REIST_NVIDIA_GK208_USERD_BYTES / sizeof(uint32_t))
#define REIST_NVIDIA_GK208_RUNLIST_WORDS \
    (REIST_NVIDIA_GK208_RUNLIST_BYTES / sizeof(uint32_t))

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

typedef struct {
    uint32_t words[REIST_NVIDIA_GK208_SUBMISSION_WORD_CAPACITY];
    uint32_t word_count;
    uint32_t gpfifo_entry[REIST_NVIDIA_GK208_GPFIFO_ENTRY_WORDS];
} reist_nvidia_gk208_submission_t;

typedef struct {
    uint32_t gpfifo_offset;
    uint32_t gpfifo_bytes;
    uint32_t pushbuf_offset;
    uint32_t pushbuf_bytes;
    uint32_t fence_offset;
    uint32_t fence_bytes;
    uint32_t fence_sequence;
    uint32_t reserved;
} reist_nvidia_gk208_dma_staging_t;

typedef struct {
    uint32_t destination_pool_offset;
    uint32_t source_pool_offset;
    uint32_t width;
    uint32_t reserved;
} reist_nvidia_gk208_address_relocation_t;

typedef struct {
    uint32_t ramfc[REIST_NVIDIA_GK208_RAMFC_WORDS];
    uint32_t userd[REIST_NVIDIA_GK208_USERD_WORDS];
    uint32_t runlist[REIST_NVIDIA_GK208_RUNLIST_WORDS];
    uint32_t ramfc_pool_offset;
    uint32_t ramfc_bytes;
    uint32_t userd_pool_offset;
    uint32_t userd_bytes;
    uint32_t runlist_pool_offset;
    uint32_t runlist_bytes;
    uint32_t channel_id;
    uint32_t gpfifo_bytes;
    reist_nvidia_gk208_address_relocation_t userd_relocation;
} reist_nvidia_gk208_channel_image_t;

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
int reist_nvidia_gk208_prepare_submission(
    reist_nvidia_gk208_submission_t *submission,
    const reist_nvidia_gk208_pushbuf_t *commands,
    uint64_t pushbuf_gpu_address, uint64_t fence_gpu_address,
    uint32_t fence_sequence);
int reist_nvidia_gk208_validate_submission(
    const reist_nvidia_gk208_submission_t *submission,
    uint64_t pushbuf_gpu_address, uint64_t fence_gpu_address,
    uint32_t fence_sequence);
int reist_nvidia_gk208_prepare_dma_staging(
    reist_nvidia_gk208_dma_staging_t *staging,
    const reist_nvidia_gk208_submission_t *submission,
    uint32_t fence_sequence);
int reist_nvidia_gk208_validate_dma_staging(
    const reist_nvidia_gk208_dma_staging_t *staging,
    const reist_nvidia_gk208_submission_t *submission,
    uint32_t fence_sequence);
int reist_nvidia_gk208_prepare_channel_image(
    reist_nvidia_gk208_channel_image_t *image);
int reist_nvidia_gk208_validate_channel_image(
    const reist_nvidia_gk208_channel_image_t *image);
int reist_nvidia_gk208_command_self_test(void);
int reist_nvidia_gk208_submission_self_test(void);
int reist_nvidia_gk208_dma_staging_self_test(void);
int reist_nvidia_gk208_channel_image_self_test(void);

#endif
