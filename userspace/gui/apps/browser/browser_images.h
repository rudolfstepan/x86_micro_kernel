#ifndef REIST_BROWSER_IMAGES_H
#define REIST_BROWSER_IMAGES_H
#include <stddef.h>
#include <stdint.h>
#include "reist/image.h"
#define BROWSER_IMAGE_INPUT_LIMIT 262144U
#define BROWSER_IMAGE_PIXEL_LIMIT (1024U * 768U)
#define BROWSER_DECODE_ARENA_BYTES (12U * 1024U * 1024U)
/* Caller reserves this fixed quota once, before any untrusted decode. */
int browser_image_workspace(void *storage, size_t size);
/* Single browser event-loop owner. Never reentrant; no global device access. */
int browser_image_decode(const uint8_t *encoded, size_t length,
                          uint32_t *pixels, size_t capacity,
                          reist_image_info_t *info);
#endif
