/** Bounded startup-mode adapter v1. No addresses or device authority. */
#ifndef REIST_DISPLAY_MODE_H
#define REIST_DISPLAY_MODE_H
#include <stdint.h>

#define REIST_DISPLAY_MODE_VERSION 1U
#define REIST_DISPLAY_MODE_QUERY 13U
#define REIST_DISPLAY_MODE_ACTIVATE 14U
#define REIST_DISPLAY_BACKEND_DISPI 1U
#define REIST_DISPLAY_BACKEND_SVGA2 2U
#define REIST_DISPLAY_BACKEND_VBE 3U
#define REIST_DISPLAY_MODE_ACTIVE 1U
#define REIST_DISPLAY_MODE_SHADOW_BYTES (16U * 1024U * 1024U)
#define REIST_DISPLAY_MODE_MAX_DIMENSION 4096U

typedef struct {
    uint32_t version, struct_size, operation, reserved;
    uint32_t backend, max_width, max_height, scanout_bytes, shadow_bytes;
    uint32_t fixed_width, fixed_height;
    uint32_t width, height, bpp, flags, reserved2;
} reist_display_mode_request_t;

#if defined(__cplusplus)
static_assert(sizeof(reist_display_mode_request_t) == 64U, "display mode ABI");
#else
_Static_assert(sizeof(reist_display_mode_request_t) == 64U, "display mode ABI");
#endif

/* Pure admission shared by the mediator and Ring-3 hint filtering. Actual
 * activation must independently recheck hardware and post-enable pitch. */
static inline int reist_display_geometry_fits(
        uint32_t width, uint32_t height, uint32_t pitch,
        uint32_t max_width, uint32_t max_height,
        uint32_t scanout_bytes, uint32_t shadow_bytes) {
    if (width < 800U || height < 600U || width > max_width || height > max_height ||
        width > REIST_DISPLAY_MODE_MAX_DIMENSION ||
        height > REIST_DISPLAY_MODE_MAX_DIMENSION ||
        pitch < (uint64_t)width * 4U || !scanout_bytes ||
        !shadow_bytes || shadow_bytes > REIST_DISPLAY_MODE_SHADOW_BYTES)
        return 0;
    uint64_t bytes = (uint64_t)pitch * height;
    return bytes <= scanout_bytes && bytes <= shadow_bytes;
}

static inline int reist_display_mode_supported(uint32_t width, uint32_t height,
                                               const reist_display_mode_request_t *caps) {
    if (!caps || caps->version != REIST_DISPLAY_MODE_VERSION ||
        caps->struct_size != sizeof(*caps) || caps->operation != REIST_DISPLAY_MODE_QUERY ||
        caps->reserved || caps->reserved2 || caps->bpp != 32U ||
        (caps->flags & ~REIST_DISPLAY_MODE_ACTIVE) ||
        caps->backend < REIST_DISPLAY_BACKEND_DISPI || caps->backend > REIST_DISPLAY_BACKEND_VBE ||
        width > UINT32_MAX / 4U) return 0;
    if (caps->backend == REIST_DISPLAY_BACKEND_DISPI && (width & 7U)) return 0;
    if (caps->backend == REIST_DISPLAY_BACKEND_VBE &&
        (width != caps->fixed_width || height != caps->fixed_height)) return 0;
    return reist_display_geometry_fits(width, height, width * 4U,
        caps->max_width, caps->max_height, caps->scanout_bytes, caps->shadow_bytes);
}
#endif
