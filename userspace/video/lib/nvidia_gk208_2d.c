/**
 * @file nvidia_gk208_2d.c
 * @brief Fail-closed FERMI_TWOD_A method compiler for GK208.
 *
 * Method numbers and packet fields follow the MIT-licensed NVIDIA class
 * headers carried by upstream Nouveau (cl902d.h and cl906f.h).  Only the
 * subset required for pitch-linear XRGB8888 fill and same-surface copy is
 * admitted here.
 */
#include "../include/reist/nvidia_gk208_2d.h"

#include <stddef.h>
#include <stdint.h>

#define NV902D_SET_DST_FORMAT 0x0200U
#define NV902D_SET_DST_MEMORY_LAYOUT 0x0204U
#define NV902D_SET_DST_PITCH 0x0214U
#define NV902D_SET_DST_WIDTH 0x0218U
#define NV902D_SET_DST_HEIGHT 0x021CU
#define NV902D_SET_DST_OFFSET_UPPER 0x0220U
#define NV902D_SET_DST_OFFSET_LOWER 0x0224U
#define NV902D_SET_SRC_FORMAT 0x0230U
#define NV902D_SET_SRC_MEMORY_LAYOUT 0x0234U
#define NV902D_SET_SRC_PITCH 0x0244U
#define NV902D_SET_SRC_WIDTH 0x0248U
#define NV902D_SET_SRC_HEIGHT 0x024CU
#define NV902D_SET_SRC_OFFSET_UPPER 0x0250U
#define NV902D_SET_SRC_OFFSET_LOWER 0x0254U
#define NV902D_SET_CLIP_ENABLE 0x0290U
#define NV902D_SET_OPERATION 0x02ACU
#define NV902D_RENDER_SOLID_PRIM_MODE 0x0580U
#define NV902D_SET_RENDER_SOLID_PRIM_COLOR_FORMAT 0x0584U
#define NV902D_SET_RENDER_SOLID_PRIM_COLOR 0x0588U
#define NV902D_RENDER_SOLID_PRIM_POINT_SET_X0 0x0600U
#define NV902D_RENDER_SOLID_PRIM_POINT_Y0 0x0604U
#define NV902D_RENDER_SOLID_PRIM_POINT_SET_X1 0x0608U
#define NV902D_RENDER_SOLID_PRIM_POINT_Y1 0x060CU
#define NV902D_SET_PIXELS_FROM_MEMORY_SAFE_OVERLAP 0x0888U
#define NV902D_SET_PIXELS_FROM_MEMORY_DST_X0 0x08B0U
#define NV902D_SET_PIXELS_FROM_MEMORY_DST_Y0 0x08B4U
#define NV902D_SET_PIXELS_FROM_MEMORY_DST_WIDTH 0x08B8U
#define NV902D_SET_PIXELS_FROM_MEMORY_DST_HEIGHT 0x08BCU
#define NV902D_SET_PIXELS_FROM_MEMORY_DU_DX_FRAC 0x08C0U
#define NV902D_SET_PIXELS_FROM_MEMORY_DU_DX_INT 0x08C4U
#define NV902D_SET_PIXELS_FROM_MEMORY_DV_DY_FRAC 0x08C8U
#define NV902D_SET_PIXELS_FROM_MEMORY_DV_DY_INT 0x08CCU
#define NV902D_SET_PIXELS_FROM_MEMORY_SRC_X0_FRAC 0x08D0U
#define NV902D_SET_PIXELS_FROM_MEMORY_SRC_X0_INT 0x08D4U
#define NV902D_SET_PIXELS_FROM_MEMORY_SRC_Y0_FRAC 0x08D8U
#define NV902D_PIXELS_FROM_MEMORY_SRC_Y0_INT 0x08DCU

#define NV902D_FORMAT_X8R8G8B8 0x000000E6U
#define NV902D_MEMORY_LAYOUT_PITCH 0x00000001U
#define NV902D_OPERATION_SRCCOPY 0x00000003U
#define NV902D_SOLID_PRIM_RECTS 0x00000004U
#define NV902D_SAFE_OVERLAP_TRUE 0x00000001U
#define NV906F_DMA_SEC_OP_INC_METHOD 0x00000001U
#define NVIDIA_GK208_PACKET_WORDS 2U
#define NVIDIA_GK208_FILL_PACKET_COUNT 16U
#define NVIDIA_GK208_COPY_PACKET_COUNT 29U
#define NVIDIA_GK208_ADDRESS_LIMIT (1ULL << 40U)

static const uint16_t fill_methods[NVIDIA_GK208_FILL_PACKET_COUNT] = {
    NV902D_SET_DST_FORMAT, NV902D_SET_DST_MEMORY_LAYOUT,
    NV902D_SET_DST_PITCH, NV902D_SET_DST_WIDTH, NV902D_SET_DST_HEIGHT,
    NV902D_SET_DST_OFFSET_UPPER, NV902D_SET_DST_OFFSET_LOWER,
    NV902D_SET_CLIP_ENABLE, NV902D_SET_OPERATION,
    NV902D_RENDER_SOLID_PRIM_MODE,
    NV902D_SET_RENDER_SOLID_PRIM_COLOR_FORMAT,
    NV902D_SET_RENDER_SOLID_PRIM_COLOR,
    NV902D_RENDER_SOLID_PRIM_POINT_SET_X0,
    NV902D_RENDER_SOLID_PRIM_POINT_Y0,
    NV902D_RENDER_SOLID_PRIM_POINT_SET_X1,
    NV902D_RENDER_SOLID_PRIM_POINT_Y1,
};

static const uint16_t copy_methods[NVIDIA_GK208_COPY_PACKET_COUNT] = {
    NV902D_SET_DST_FORMAT, NV902D_SET_DST_MEMORY_LAYOUT,
    NV902D_SET_DST_PITCH, NV902D_SET_DST_WIDTH, NV902D_SET_DST_HEIGHT,
    NV902D_SET_DST_OFFSET_UPPER, NV902D_SET_DST_OFFSET_LOWER,
    NV902D_SET_SRC_FORMAT, NV902D_SET_SRC_MEMORY_LAYOUT,
    NV902D_SET_SRC_PITCH, NV902D_SET_SRC_WIDTH, NV902D_SET_SRC_HEIGHT,
    NV902D_SET_SRC_OFFSET_UPPER, NV902D_SET_SRC_OFFSET_LOWER,
    NV902D_SET_CLIP_ENABLE, NV902D_SET_OPERATION,
    NV902D_SET_PIXELS_FROM_MEMORY_SAFE_OVERLAP,
    NV902D_SET_PIXELS_FROM_MEMORY_DST_X0,
    NV902D_SET_PIXELS_FROM_MEMORY_DST_Y0,
    NV902D_SET_PIXELS_FROM_MEMORY_DST_WIDTH,
    NV902D_SET_PIXELS_FROM_MEMORY_DST_HEIGHT,
    NV902D_SET_PIXELS_FROM_MEMORY_DU_DX_FRAC,
    NV902D_SET_PIXELS_FROM_MEMORY_DU_DX_INT,
    NV902D_SET_PIXELS_FROM_MEMORY_DV_DY_FRAC,
    NV902D_SET_PIXELS_FROM_MEMORY_DV_DY_INT,
    NV902D_SET_PIXELS_FROM_MEMORY_SRC_X0_FRAC,
    NV902D_SET_PIXELS_FROM_MEMORY_SRC_X0_INT,
    NV902D_SET_PIXELS_FROM_MEMORY_SRC_Y0_FRAC,
    NV902D_PIXELS_FROM_MEMORY_SRC_Y0_INT,
};

static void pushbuf_reset(reist_nvidia_gk208_pushbuf_t *pushbuf) {
    for (uint32_t index = 0U;
         index < REIST_NVIDIA_GK208_PUSHBUF_WORD_CAPACITY; ++index)
        pushbuf->words[index] = 0U;
    pushbuf->word_count = 0U;
}

static uint32_t method_header(uint32_t method) {
    return (NV906F_DMA_SEC_OP_INC_METHOD << 29U) |
           (1U << 16U) | (REIST_NVIDIA_GK208_2D_SUBCHANNEL << 13U) |
           (method >> 2U);
}

static int emit(reist_nvidia_gk208_pushbuf_t *pushbuf,
                uint32_t method, uint32_t value) {
    if (pushbuf->word_count > REIST_NVIDIA_GK208_PUSHBUF_WORD_CAPACITY -
                                  NVIDIA_GK208_PACKET_WORDS)
        return -28;
    pushbuf->words[pushbuf->word_count++] = method_header(method);
    pushbuf->words[pushbuf->word_count++] = value;
    return 0;
}

static int surface_valid(const reist_nvidia_gk208_surface_t *surface) {
    if (surface == NULL || surface->width == 0U || surface->height == 0U ||
        surface->width > REIST_NVIDIA_GK208_MAX_DIMENSION ||
        surface->height > REIST_NVIDIA_GK208_MAX_DIMENSION ||
        surface->pitch > REIST_NVIDIA_GK208_MAX_PITCH ||
        (surface->pitch & 3U) != 0U ||
        surface->width > UINT32_MAX / 4U ||
        surface->pitch < surface->width * 4U ||
        (surface->gpu_address &
         (REIST_NVIDIA_GK208_SURFACE_ALIGNMENT - 1U)) != 0U ||
        surface->gpu_address >= NVIDIA_GK208_ADDRESS_LIMIT)
        return 0;
    uint64_t bytes = (uint64_t)surface->pitch * surface->height;
    return bytes <= NVIDIA_GK208_ADDRESS_LIMIT - surface->gpu_address;
}

static int rect_valid(const reist_nvidia_gk208_surface_t *surface,
                      const reist_nvidia_gk208_rect_t *rect) {
    return rect != NULL && rect->width != 0U && rect->height != 0U &&
        rect->x < surface->width && rect->y < surface->height &&
        rect->width <= surface->width - rect->x &&
        rect->height <= surface->height - rect->y;
}

static int emit_surface(reist_nvidia_gk208_pushbuf_t *pushbuf,
                        const reist_nvidia_gk208_surface_t *surface,
                        uint32_t source) {
    uint32_t format = source != 0U ? NV902D_SET_SRC_FORMAT
                                   : NV902D_SET_DST_FORMAT;
    uint32_t layout = source != 0U ? NV902D_SET_SRC_MEMORY_LAYOUT
                                   : NV902D_SET_DST_MEMORY_LAYOUT;
    uint32_t pitch = source != 0U ? NV902D_SET_SRC_PITCH
                                  : NV902D_SET_DST_PITCH;
    uint32_t width = source != 0U ? NV902D_SET_SRC_WIDTH
                                  : NV902D_SET_DST_WIDTH;
    uint32_t height = source != 0U ? NV902D_SET_SRC_HEIGHT
                                   : NV902D_SET_DST_HEIGHT;
    uint32_t upper = source != 0U ? NV902D_SET_SRC_OFFSET_UPPER
                                  : NV902D_SET_DST_OFFSET_UPPER;
    uint32_t lower = source != 0U ? NV902D_SET_SRC_OFFSET_LOWER
                                  : NV902D_SET_DST_OFFSET_LOWER;
    if (emit(pushbuf, format, NV902D_FORMAT_X8R8G8B8) != 0 ||
        emit(pushbuf, layout, NV902D_MEMORY_LAYOUT_PITCH) != 0 ||
        emit(pushbuf, pitch, surface->pitch) != 0 ||
        emit(pushbuf, width, surface->width) != 0 ||
        emit(pushbuf, height, surface->height) != 0 ||
        emit(pushbuf, upper, (uint32_t)(surface->gpu_address >> 32U)) != 0 ||
        emit(pushbuf, lower, (uint32_t)surface->gpu_address) != 0)
        return -28;
    return 0;
}

int reist_nvidia_gk208_encode_fill(
    reist_nvidia_gk208_pushbuf_t *pushbuf,
    const reist_nvidia_gk208_surface_t *surface,
    const reist_nvidia_gk208_rect_t *destination,
    uint32_t xrgb8888) {
    if (pushbuf == NULL || surface == NULL || destination == NULL) return -22;
    pushbuf_reset(pushbuf);
    if (!surface_valid(surface) || !rect_valid(surface, destination) ||
        (xrgb8888 & 0xFF000000U) != 0U)
        return -34;
    uint32_t right = destination->x + destination->width;
    uint32_t bottom = destination->y + destination->height;
    if (emit_surface(pushbuf, surface, 0U) != 0 ||
        emit(pushbuf, NV902D_SET_CLIP_ENABLE, 0U) != 0 ||
        emit(pushbuf, NV902D_SET_OPERATION, NV902D_OPERATION_SRCCOPY) != 0 ||
        emit(pushbuf, NV902D_RENDER_SOLID_PRIM_MODE,
             NV902D_SOLID_PRIM_RECTS) != 0 ||
        emit(pushbuf, NV902D_SET_RENDER_SOLID_PRIM_COLOR_FORMAT,
             NV902D_FORMAT_X8R8G8B8) != 0 ||
        emit(pushbuf, NV902D_SET_RENDER_SOLID_PRIM_COLOR, xrgb8888) != 0 ||
        emit(pushbuf, NV902D_RENDER_SOLID_PRIM_POINT_SET_X0,
             destination->x) != 0 ||
        emit(pushbuf, NV902D_RENDER_SOLID_PRIM_POINT_Y0,
             destination->y) != 0 ||
        emit(pushbuf, NV902D_RENDER_SOLID_PRIM_POINT_SET_X1, right) != 0 ||
        emit(pushbuf, NV902D_RENDER_SOLID_PRIM_POINT_Y1, bottom) != 0)
        return -28;
    return reist_nvidia_gk208_validate_pushbuf(pushbuf);
}

int reist_nvidia_gk208_encode_copy(
    reist_nvidia_gk208_pushbuf_t *pushbuf,
    const reist_nvidia_gk208_surface_t *surface,
    const reist_nvidia_gk208_rect_t *source,
    const reist_nvidia_gk208_rect_t *destination) {
    if (pushbuf == NULL || surface == NULL || source == NULL ||
        destination == NULL)
        return -22;
    pushbuf_reset(pushbuf);
    if (!surface_valid(surface) || !rect_valid(surface, source) ||
        !rect_valid(surface, destination) ||
        source->width != destination->width ||
        source->height != destination->height)
        return -34;
    if (emit_surface(pushbuf, surface, 0U) != 0 ||
        emit_surface(pushbuf, surface, 1U) != 0 ||
        emit(pushbuf, NV902D_SET_CLIP_ENABLE, 0U) != 0 ||
        emit(pushbuf, NV902D_SET_OPERATION, NV902D_OPERATION_SRCCOPY) != 0 ||
        emit(pushbuf, NV902D_SET_PIXELS_FROM_MEMORY_SAFE_OVERLAP,
             NV902D_SAFE_OVERLAP_TRUE) != 0 ||
        emit(pushbuf, NV902D_SET_PIXELS_FROM_MEMORY_DST_X0,
             destination->x) != 0 ||
        emit(pushbuf, NV902D_SET_PIXELS_FROM_MEMORY_DST_Y0,
             destination->y) != 0 ||
        emit(pushbuf, NV902D_SET_PIXELS_FROM_MEMORY_DST_WIDTH,
             destination->width) != 0 ||
        emit(pushbuf, NV902D_SET_PIXELS_FROM_MEMORY_DST_HEIGHT,
             destination->height) != 0 ||
        emit(pushbuf, NV902D_SET_PIXELS_FROM_MEMORY_DU_DX_FRAC, 0U) != 0 ||
        emit(pushbuf, NV902D_SET_PIXELS_FROM_MEMORY_DU_DX_INT, 1U) != 0 ||
        emit(pushbuf, NV902D_SET_PIXELS_FROM_MEMORY_DV_DY_FRAC, 0U) != 0 ||
        emit(pushbuf, NV902D_SET_PIXELS_FROM_MEMORY_DV_DY_INT, 1U) != 0 ||
        emit(pushbuf, NV902D_SET_PIXELS_FROM_MEMORY_SRC_X0_FRAC, 0U) != 0 ||
        emit(pushbuf, NV902D_SET_PIXELS_FROM_MEMORY_SRC_X0_INT,
             source->x) != 0 ||
        emit(pushbuf, NV902D_SET_PIXELS_FROM_MEMORY_SRC_Y0_FRAC, 0U) != 0 ||
        emit(pushbuf, NV902D_PIXELS_FROM_MEMORY_SRC_Y0_INT,
             source->y) != 0)
        return -28;
    return reist_nvidia_gk208_validate_pushbuf(pushbuf);
}

static uint32_t packet_value(const reist_nvidia_gk208_pushbuf_t *pushbuf,
                             uint32_t packet) {
    return pushbuf->words[packet * NVIDIA_GK208_PACKET_WORDS + 1U];
}

static int methods_match(const reist_nvidia_gk208_pushbuf_t *pushbuf,
                         const uint16_t *methods, uint32_t count) {
    if (pushbuf->word_count != count * NVIDIA_GK208_PACKET_WORDS) return 0;
    for (uint32_t index = 0U; index < count; ++index) {
        if (pushbuf->words[index * NVIDIA_GK208_PACKET_WORDS] !=
            method_header(methods[index]))
            return 0;
    }
    return 1;
}

static int surface_values_valid(
    const reist_nvidia_gk208_pushbuf_t *pushbuf, uint32_t first) {
    uint32_t pitch = packet_value(pushbuf, first + 2U);
    uint32_t width = packet_value(pushbuf, first + 3U);
    uint32_t height = packet_value(pushbuf, first + 4U);
    uint64_t address = ((uint64_t)packet_value(pushbuf, first + 5U) << 32U) |
                       packet_value(pushbuf, first + 6U);
    return packet_value(pushbuf, first) == NV902D_FORMAT_X8R8G8B8 &&
        packet_value(pushbuf, first + 1U) == NV902D_MEMORY_LAYOUT_PITCH &&
        width != 0U && height != 0U &&
        width <= REIST_NVIDIA_GK208_MAX_DIMENSION &&
        height <= REIST_NVIDIA_GK208_MAX_DIMENSION &&
        pitch <= REIST_NVIDIA_GK208_MAX_PITCH && (pitch & 3U) == 0U &&
        pitch >= width * 4U && address < NVIDIA_GK208_ADDRESS_LIMIT &&
        (address & (REIST_NVIDIA_GK208_SURFACE_ALIGNMENT - 1U)) == 0U &&
        (uint64_t)pitch * height <= NVIDIA_GK208_ADDRESS_LIMIT - address;
}

static int fill_values_valid(const reist_nvidia_gk208_pushbuf_t *pushbuf) {
    uint32_t width = packet_value(pushbuf, 3U);
    uint32_t height = packet_value(pushbuf, 4U);
    uint32_t x0 = packet_value(pushbuf, 12U);
    uint32_t y0 = packet_value(pushbuf, 13U);
    uint32_t x1 = packet_value(pushbuf, 14U);
    uint32_t y1 = packet_value(pushbuf, 15U);
    return surface_values_valid(pushbuf, 0U) &&
        packet_value(pushbuf, 7U) == 0U &&
        packet_value(pushbuf, 8U) == NV902D_OPERATION_SRCCOPY &&
        packet_value(pushbuf, 9U) == NV902D_SOLID_PRIM_RECTS &&
        packet_value(pushbuf, 10U) == NV902D_FORMAT_X8R8G8B8 &&
        (packet_value(pushbuf, 11U) & 0xFF000000U) == 0U &&
        x0 < x1 && y0 < y1 && x1 <= width && y1 <= height;
}

static int copy_values_valid(const reist_nvidia_gk208_pushbuf_t *pushbuf) {
    uint32_t width = packet_value(pushbuf, 3U);
    uint32_t height = packet_value(pushbuf, 4U);
    uint32_t dst_x = packet_value(pushbuf, 17U);
    uint32_t dst_y = packet_value(pushbuf, 18U);
    uint32_t copy_width = packet_value(pushbuf, 19U);
    uint32_t copy_height = packet_value(pushbuf, 20U);
    uint32_t src_x = packet_value(pushbuf, 26U);
    uint32_t src_y = packet_value(pushbuf, 28U);
    return surface_values_valid(pushbuf, 0U) &&
        surface_values_valid(pushbuf, 7U) &&
        packet_value(pushbuf, 5U) == packet_value(pushbuf, 12U) &&
        packet_value(pushbuf, 6U) == packet_value(pushbuf, 13U) &&
        packet_value(pushbuf, 14U) == 0U &&
        packet_value(pushbuf, 15U) == NV902D_OPERATION_SRCCOPY &&
        packet_value(pushbuf, 16U) == NV902D_SAFE_OVERLAP_TRUE &&
        copy_width != 0U && copy_height != 0U &&
        dst_x < width && dst_y < height && src_x < width && src_y < height &&
        copy_width <= width - dst_x && copy_width <= width - src_x &&
        copy_height <= height - dst_y && copy_height <= height - src_y &&
        packet_value(pushbuf, 21U) == 0U &&
        packet_value(pushbuf, 22U) == 1U &&
        packet_value(pushbuf, 23U) == 0U &&
        packet_value(pushbuf, 24U) == 1U &&
        packet_value(pushbuf, 25U) == 0U &&
        packet_value(pushbuf, 27U) == 0U;
}

int reist_nvidia_gk208_validate_pushbuf(
    const reist_nvidia_gk208_pushbuf_t *pushbuf) {
    if (pushbuf == NULL || pushbuf->word_count == 0U ||
        pushbuf->word_count > REIST_NVIDIA_GK208_PUSHBUF_WORD_CAPACITY ||
        (pushbuf->word_count & 1U) != 0U)
        return -84;
    if (methods_match(pushbuf, fill_methods,
                      NVIDIA_GK208_FILL_PACKET_COUNT))
        return fill_values_valid(pushbuf) ? 0 : -84;
    if (methods_match(pushbuf, copy_methods,
                      NVIDIA_GK208_COPY_PACKET_COUNT))
        return copy_values_valid(pushbuf) ? 0 : -84;
    return -84;
}

int reist_nvidia_gk208_command_self_test(void) {
    reist_nvidia_gk208_pushbuf_t pushbuf;
    const reist_nvidia_gk208_surface_t surface = {
        .gpu_address = 0x10000000ULL,
        .width = 1024U,
        .height = 768U,
        .pitch = 4096U,
    };
    const reist_nvidia_gk208_rect_t source = {0U, 0U, 8U, 8U};
    const reist_nvidia_gk208_rect_t destination = {8U, 8U, 8U, 8U};
    if (reist_nvidia_gk208_encode_fill(
            &pushbuf, &surface, &destination, 0x00123456U) != 0 ||
        reist_nvidia_gk208_validate_pushbuf(&pushbuf) != 0)
        return -84;
    if (reist_nvidia_gk208_encode_copy(
            &pushbuf, &surface, &source, &destination) != 0 ||
        reist_nvidia_gk208_validate_pushbuf(&pushbuf) != 0)
        return -84;
    return 0;
}
