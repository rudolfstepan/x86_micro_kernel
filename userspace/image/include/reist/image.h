/**
 * @file reist/image.h
 * @brief Bounded, reentrant raster-image decoding API.
 *
 * Decoders consume caller-owned immutable bytes and publish XRGB8888 pixels
 * only after validating dimensions and output capacity. No global state,
 * allocation, filesystem access or display access is used by this layer.
 */
#ifndef REIST_IMAGE_H
#define REIST_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#define REIST_IMAGE_API_VERSION 1U
#define REIST_IMAGE_MAX_WIDTH 1024U
#define REIST_IMAGE_MAX_HEIGHT 768U
#define REIST_IMAGE_GIF_TABLE_CAPACITY 4096U
#define REIST_IMAGE_WORKSPACE_BYTES (REIST_IMAGE_MAX_WIDTH * REIST_IMAGE_MAX_HEIGHT)

enum reist_image_format {
    REIST_IMAGE_FORMAT_BMP = 1U,
    REIST_IMAGE_FORMAT_GIF = 2U,
};

typedef struct reist_image_info {
    uint32_t version;
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t format;
    uint32_t frame_count;
    uint32_t flags;
} reist_image_info_t;

typedef struct reist_image_workspace {
    uint8_t bytes[REIST_IMAGE_WORKSPACE_BYTES];
    uint16_t prefix[REIST_IMAGE_GIF_TABLE_CAPACITY];
    uint8_t suffix[REIST_IMAGE_GIF_TABLE_CAPACITY];
    uint8_t stack[REIST_IMAGE_GIF_TABLE_CAPACITY];
} reist_image_workspace_t;

_Static_assert(sizeof(reist_image_info_t) == 32U,
               "image information ABI changed");

/**
 * Decode a BMP or the first composited GIF frame into XRGB8888 pixels.
 * Supported BMP input: Windows DIB, uncompressed 24/32-bit BI_RGB.
 * Supported GIF input: GIF87a/GIF89a, global/local palettes, interlace,
 * transparency and standard 12-bit LZW. Later animation frames are counted
 * but intentionally not rendered by API version 1.
 */
int reist_image_decode(const uint8_t *encoded, size_t encoded_size,
                       uint32_t *pixels, size_t pixel_capacity,
                       reist_image_workspace_t *workspace,
                       reist_image_info_t *info);

#endif
