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
#include "nvidia_gk208_firmware_data.h"
#include "nvidia_gk208_gr_tables.h"

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
#define NV906F_SET_OBJECT 0x00000000U
#define NV906F_SEMAPHOREA 0x00000010U
#define NV906F_SEMAPHORED_OPERATION_RELEASE 0x00000002U
#define NV906F_SEMAPHORED_RELEASE_SIZE_4BYTE (1U << 24U)
#define NV906F_DMA_SEC_OP_INC_METHOD 0x00000001U
#define NVIDIA_GK208_PACKET_WORDS 2U
#define NVIDIA_GK208_FILL_PACKET_COUNT 16U
#define NVIDIA_GK208_COPY_PACKET_COUNT 29U
#define NVIDIA_GK208_CLASS_BIND_WORDS 2U
#define NVIDIA_GK208_FENCE_WORDS 5U
#define NVIDIA_GK208_ADDRESS_LIMIT (1ULL << 40U)
#define NVIDIA_GK208_GPFIFO_LIMIT2 9U
#define NVIDIA_GK208_RAMFC_USERD_LOW_WORD (0x08U / sizeof(uint32_t))
#define NVIDIA_GK208_RAMFC_USERD_HIGH_WORD (0x0CU / sizeof(uint32_t))

_Static_assert(REIST_NVIDIA_GK208_GPFIFO_BYTES ==
                   (8U << NVIDIA_GK208_GPFIFO_LIMIT2),
               "GK208 GPFIFO limit2 no longer matches its fixed size");
_Static_assert(REIST_NVIDIA_GK208_CHANNEL_ID <
                   REIST_NVIDIA_GK208_CHANNEL_LIMIT,
               "GK208 fixed channel ID is outside the hardware range");
_Static_assert(sizeof(reist_nvidia_gk208_gr_firmware_manifest_t) == 64U,
               "GK208 firmware manifest ABI must remain fixed");
_Static_assert(sizeof(reist_nvidia_gk208_gr_plan_manifest_t) == 64U,
               "GK208 GR plan manifest ABI must remain fixed");
_Static_assert(sizeof(reist_nvidia_gk208_gr_execution_op_t) ==
                   REIST_NVIDIA_GK208_GR_EXECUTION_OP_BYTES,
               "GK208 GR execution operation ABI must remain fixed");
_Static_assert(sizeof(reist_nvidia_gk208_gr_execution_header_t) ==
                   REIST_NVIDIA_GK208_GR_EXECUTION_HEADER_BYTES,
               "GK208 GR execution header ABI must remain fixed");
_Static_assert(sizeof(reist_nvidia_gk208_gr_execution_image_t) ==
                   REIST_NVIDIA_GK208_GR_EXECUTION_MAX_BYTES,
               "GK208 GR execution image capacity must remain fixed");
_Static_assert(sizeof(reist_nvidia_gk208_gr_context_memory_plan_t) == 64U,
               "GK208 context-memory plan ABI must remain fixed");
_Static_assert(sizeof(reist_nvidia_gk208_gr_golden_map_t) == 32U,
               "GK208 golden-context map ABI must remain fixed");
_Static_assert(sizeof(reist_nvidia_gk208_gr_golden_patch_t) == 16U,
               "GK208 golden-context patch ABI must remain fixed");
_Static_assert(sizeof(reist_nvidia_gk208_gr_method_tuple_t) == 20U,
               "GK208 golden-context method ABI must remain fixed");
_Static_assert(sizeof(reist_nvidia_gk208_gr_golden_plan_t) == 1776U,
               "GK208 golden-context plan ABI must remain fixed");
_Static_assert(REIST_NVIDIA_GK208_DMA_GR_EXECUTION_OFFSET +
                   REIST_NVIDIA_GK208_GR_EXECUTION_MAX_BYTES <=
                   REIST_NVIDIA_GK208_DMA_POOL_BYTES,
               "GK208 GR execution image exceeds the mediated DMA pool");
_Static_assert(REIST_GK208_GR_MMIO_PACK_COUNT ==
                   REIST_NVIDIA_GK208_GR_MMIO_PACK_COUNT,
               "GK208 MMIO pack count changed");
_Static_assert(REIST_GK208_GR_CONTEXT_PACK_COUNT ==
                   REIST_NVIDIA_GK208_GR_CONTEXT_PACK_COUNT,
               "GK208 context pack count changed");
_Static_assert(REIST_GK208_GR_ICMD_TUPLE_COUNT != 0U &&
                   REIST_GK208_GR_MTHD_TUPLE_COUNT != 0U,
               "GK208 golden-context indirect tables are empty");
_Static_assert(sizeof(gk208_grhub_data) / sizeof(gk208_grhub_data[0]) ==
                   REIST_NVIDIA_GK208_GR_FECS_DATA_WORDS,
               "GK208 FECS data image size changed");
_Static_assert(sizeof(gk208_grhub_code) / sizeof(gk208_grhub_code[0]) ==
                   REIST_NVIDIA_GK208_GR_FECS_CODE_WORDS,
               "GK208 FECS code image size changed");
_Static_assert(sizeof(gk208_grgpc_data) / sizeof(gk208_grgpc_data[0]) ==
                   REIST_NVIDIA_GK208_GR_GPCCS_DATA_WORDS,
               "GK208 GPCCS data image size changed");
_Static_assert(sizeof(gk208_grgpc_code) / sizeof(gk208_grgpc_code[0]) ==
                   REIST_NVIDIA_GK208_GR_GPCCS_CODE_WORDS,
               "GK208 GPCCS code image size changed");

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

static uint32_t method_header_for(uint32_t method, uint32_t subchannel,
                                  uint32_t count) {
    return (NV906F_DMA_SEC_OP_INC_METHOD << 29U) |
           (count << 16U) | (subchannel << 13U) |
           (method >> 2U);
}

static uint32_t method_header(uint32_t method) {
    return method_header_for(
        method, REIST_NVIDIA_GK208_2D_SUBCHANNEL, 1U);
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

static void submission_reset(reist_nvidia_gk208_submission_t *submission) {
    for (uint32_t index = 0U;
         index < REIST_NVIDIA_GK208_SUBMISSION_WORD_CAPACITY; ++index)
        submission->words[index] = 0U;
    submission->word_count = 0U;
    for (uint32_t index = 0U;
         index < REIST_NVIDIA_GK208_GPFIFO_ENTRY_WORDS; ++index)
        submission->gpfifo_entry[index] = 0U;
}

static int gpu_range_valid(uint64_t address, uint32_t bytes) {
    return address != 0U && (address & 3U) == 0U &&
        address < NVIDIA_GK208_ADDRESS_LIMIT && bytes != 0U &&
        bytes <= NVIDIA_GK208_ADDRESS_LIMIT - address;
}

static uint32_t submission_engine_words(
        const reist_nvidia_gk208_submission_t *submission) {
    const uint32_t envelope_words = NVIDIA_GK208_CLASS_BIND_WORDS +
        NVIDIA_GK208_FENCE_WORDS;
    return submission->word_count >= envelope_words
        ? submission->word_count - envelope_words : 0U;
}

int reist_nvidia_gk208_prepare_submission(
    reist_nvidia_gk208_submission_t *submission,
    const reist_nvidia_gk208_pushbuf_t *commands,
    uint64_t pushbuf_gpu_address, uint64_t fence_gpu_address,
    uint32_t fence_sequence) {
    if (submission == NULL || commands == NULL) return -22;
    submission_reset(submission);
    if (reist_nvidia_gk208_validate_pushbuf(commands) != 0 ||
        fence_sequence == 0U ||
        !gpu_range_valid(fence_gpu_address, sizeof(uint32_t)) ||
        commands->word_count > REIST_NVIDIA_GK208_SUBMISSION_WORD_CAPACITY -
            NVIDIA_GK208_CLASS_BIND_WORDS - NVIDIA_GK208_FENCE_WORDS)
        return -84;
    uint32_t total_words = NVIDIA_GK208_CLASS_BIND_WORDS +
        commands->word_count + NVIDIA_GK208_FENCE_WORDS;
    if (!gpu_range_valid(
            pushbuf_gpu_address, total_words * sizeof(uint32_t)))
        return -84;

    uint32_t cursor = 0U;
    submission->words[cursor++] = method_header_for(
        NV906F_SET_OBJECT, REIST_NVIDIA_GK208_2D_SUBCHANNEL, 1U);
    submission->words[cursor++] = REIST_NVIDIA_GK208_FERMI_TWOD_A;
    for (uint32_t index = 0U; index < commands->word_count; ++index)
        submission->words[cursor++] = commands->words[index];
    submission->words[cursor++] = method_header_for(
        NV906F_SEMAPHOREA, 0U, 4U);
    submission->words[cursor++] = (uint32_t)(fence_gpu_address >> 32U);
    submission->words[cursor++] = (uint32_t)fence_gpu_address;
    submission->words[cursor++] = fence_sequence;
    submission->words[cursor++] =
        NV906F_SEMAPHORED_RELEASE_SIZE_4BYTE |
        NV906F_SEMAPHORED_OPERATION_RELEASE;
    submission->word_count = cursor;
    submission->gpfifo_entry[0] = (uint32_t)pushbuf_gpu_address;
    submission->gpfifo_entry[1] =
        (uint32_t)(pushbuf_gpu_address >> 32U) |
        (submission->word_count << 10U);
    return reist_nvidia_gk208_validate_submission(
        submission, pushbuf_gpu_address, fence_gpu_address, fence_sequence);
}

int reist_nvidia_gk208_validate_submission(
    const reist_nvidia_gk208_submission_t *submission,
    uint64_t pushbuf_gpu_address, uint64_t fence_gpu_address,
    uint32_t fence_sequence) {
    if (submission == NULL || fence_sequence == 0U ||
        submission->word_count >
            REIST_NVIDIA_GK208_SUBMISSION_WORD_CAPACITY)
        return -84;
    uint32_t engine_words = submission_engine_words(submission);
    if (engine_words == 0U ||
        engine_words > REIST_NVIDIA_GK208_PUSHBUF_WORD_CAPACITY ||
        !gpu_range_valid(fence_gpu_address, sizeof(uint32_t)) ||
        !gpu_range_valid(pushbuf_gpu_address,
            submission->word_count * sizeof(uint32_t)))
        return -84;
    for (uint32_t index = submission->word_count;
         index < REIST_NVIDIA_GK208_SUBMISSION_WORD_CAPACITY; ++index)
        if (submission->words[index] != 0U) return -84;

    if (submission->words[0] != method_header_for(
            NV906F_SET_OBJECT, REIST_NVIDIA_GK208_2D_SUBCHANNEL, 1U) ||
        submission->words[1] != REIST_NVIDIA_GK208_FERMI_TWOD_A)
        return -84;
    reist_nvidia_gk208_pushbuf_t commands;
    pushbuf_reset(&commands);
    commands.word_count = engine_words;
    for (uint32_t index = 0U; index < engine_words; ++index)
        commands.words[index] =
            submission->words[NVIDIA_GK208_CLASS_BIND_WORDS + index];
    if (reist_nvidia_gk208_validate_pushbuf(&commands) != 0) return -84;

    uint32_t fence = NVIDIA_GK208_CLASS_BIND_WORDS + engine_words;
    if (submission->words[fence] != method_header_for(
            NV906F_SEMAPHOREA, 0U, 4U) ||
        submission->words[fence + 1U] !=
            (uint32_t)(fence_gpu_address >> 32U) ||
        submission->words[fence + 2U] !=
            (uint32_t)fence_gpu_address ||
        submission->words[fence + 3U] != fence_sequence ||
        submission->words[fence + 4U] !=
            (NV906F_SEMAPHORED_RELEASE_SIZE_4BYTE |
             NV906F_SEMAPHORED_OPERATION_RELEASE))
        return -84;

    uint32_t expected_entry0 = (uint32_t)pushbuf_gpu_address;
    uint32_t expected_entry1 =
        (uint32_t)(pushbuf_gpu_address >> 32U) |
        (submission->word_count << 10U);
    return submission->gpfifo_entry[0] == expected_entry0 &&
        submission->gpfifo_entry[1] == expected_entry1 ? 0 : -84;
}

static int dma_window_valid(uint32_t offset, uint32_t length) {
    return offset >= REIST_NVIDIA_GK208_DMA_DESCRIPTOR_BYTES &&
        length != 0U && offset < REIST_NVIDIA_GK208_DMA_POOL_BYTES &&
        length <= REIST_NVIDIA_GK208_DMA_POOL_BYTES - offset;
}

static int dma_windows_overlap(uint32_t first_offset, uint32_t first_length,
                               uint32_t second_offset,
                               uint32_t second_length) {
    return first_offset < second_offset + second_length &&
        second_offset < first_offset + first_length;
}

int reist_nvidia_gk208_validate_dma_staging(
    const reist_nvidia_gk208_dma_staging_t *staging,
    const reist_nvidia_gk208_submission_t *submission,
    uint32_t fence_sequence) {
    if (staging == NULL || submission == NULL || fence_sequence == 0U ||
        reist_nvidia_gk208_validate_submission(
            submission, REIST_NVIDIA_GK208_PUSHBUF_GPU_ADDRESS,
            REIST_NVIDIA_GK208_FENCE_GPU_ADDRESS, fence_sequence) != 0)
        return -84;
    if (staging->gpfifo_offset != REIST_NVIDIA_GK208_DMA_GPFIFO_OFFSET ||
        staging->gpfifo_bytes != sizeof(submission->gpfifo_entry) ||
        staging->pushbuf_offset != REIST_NVIDIA_GK208_DMA_PUSHBUF_OFFSET ||
        staging->pushbuf_bytes != sizeof(submission->words) ||
        staging->fence_offset != REIST_NVIDIA_GK208_DMA_FENCE_OFFSET ||
        staging->fence_bytes != sizeof(uint32_t) ||
        staging->fence_sequence != fence_sequence ||
        staging->reserved != 0U)
        return -84;
    if (!dma_window_valid(staging->gpfifo_offset, staging->gpfifo_bytes) ||
        !dma_window_valid(staging->pushbuf_offset, staging->pushbuf_bytes) ||
        !dma_window_valid(staging->fence_offset, staging->fence_bytes) ||
        dma_windows_overlap(staging->gpfifo_offset, staging->gpfifo_bytes,
            staging->pushbuf_offset, staging->pushbuf_bytes) ||
        dma_windows_overlap(staging->gpfifo_offset, staging->gpfifo_bytes,
            staging->fence_offset, staging->fence_bytes) ||
        dma_windows_overlap(staging->pushbuf_offset, staging->pushbuf_bytes,
            staging->fence_offset, staging->fence_bytes))
        return -84;
    return 0;
}

int reist_nvidia_gk208_prepare_dma_staging(
    reist_nvidia_gk208_dma_staging_t *staging,
    const reist_nvidia_gk208_submission_t *submission,
    uint32_t fence_sequence) {
    if (staging == NULL) return -22;
    *staging = (reist_nvidia_gk208_dma_staging_t){
        .gpfifo_offset = REIST_NVIDIA_GK208_DMA_GPFIFO_OFFSET,
        .gpfifo_bytes = sizeof(submission->gpfifo_entry),
        .pushbuf_offset = REIST_NVIDIA_GK208_DMA_PUSHBUF_OFFSET,
        .pushbuf_bytes = sizeof(submission->words),
        .fence_offset = REIST_NVIDIA_GK208_DMA_FENCE_OFFSET,
        .fence_bytes = sizeof(uint32_t),
        .fence_sequence = fence_sequence,
    };
    return reist_nvidia_gk208_validate_dma_staging(
        staging, submission, fence_sequence);
}

static uint32_t channel_ramfc_expected_word(uint32_t index) {
    switch (index * sizeof(uint32_t)) {
        case 0x10U: return 0x0000FACEU;
        case 0x30U: return 0xFFFFF902U;
        case 0x48U:
            return (uint32_t)REIST_NVIDIA_GK208_GPFIFO_GPU_ADDRESS;
        case 0x4CU:
            return (uint32_t)(REIST_NVIDIA_GK208_GPFIFO_GPU_ADDRESS >> 32U) |
                (NVIDIA_GK208_GPFIFO_LIMIT2 << 16U);
        case 0x84U: return 0x20400000U;
        case 0x94U:
            return 0x30000000U | REIST_NVIDIA_GK208_GR_DEVICE_MASK;
        case 0x9CU: return 0x00000100U;
        case 0xACU: return 0x0000001FU;
        case 0xB8U: return 0xF8000000U;
        case 0xE8U: return REIST_NVIDIA_GK208_CHANNEL_ID;
        case 0xF8U: return 0x10003080U;
        case 0xFCU: return 0x10000010U;
        default: return 0U;
    }
}

int reist_nvidia_gk208_validate_channel_image(
        const reist_nvidia_gk208_channel_image_t *image) {
    if (image == NULL ||
        image->ramfc_pool_offset != REIST_NVIDIA_GK208_DMA_RAMFC_OFFSET ||
        image->ramfc_bytes != REIST_NVIDIA_GK208_RAMFC_BYTES ||
        image->userd_pool_offset != REIST_NVIDIA_GK208_DMA_USERD_OFFSET ||
        image->userd_bytes != REIST_NVIDIA_GK208_USERD_BYTES ||
        image->runlist_pool_offset != REIST_NVIDIA_GK208_DMA_RUNLIST_OFFSET ||
        image->runlist_bytes != REIST_NVIDIA_GK208_RUNLIST_BYTES ||
        image->channel_id != REIST_NVIDIA_GK208_CHANNEL_ID ||
        image->channel_id == 0U ||
        image->channel_id >= REIST_NVIDIA_GK208_CHANNEL_LIMIT ||
        image->gpfifo_bytes != REIST_NVIDIA_GK208_GPFIFO_BYTES)
        return -84;
    if (!dma_window_valid(image->ramfc_pool_offset, image->ramfc_bytes) ||
        !dma_window_valid(image->userd_pool_offset, image->userd_bytes) ||
        !dma_window_valid(image->runlist_pool_offset, image->runlist_bytes) ||
        dma_windows_overlap(image->ramfc_pool_offset, image->ramfc_bytes,
            image->userd_pool_offset, image->userd_bytes) ||
        dma_windows_overlap(image->ramfc_pool_offset, image->ramfc_bytes,
            image->runlist_pool_offset, image->runlist_bytes) ||
        dma_windows_overlap(image->userd_pool_offset, image->userd_bytes,
            image->runlist_pool_offset, image->runlist_bytes))
        return -84;
    if (image->userd_relocation.destination_pool_offset !=
            REIST_NVIDIA_GK208_DMA_RAMFC_OFFSET + 0x08U ||
        image->userd_relocation.source_pool_offset !=
            REIST_NVIDIA_GK208_DMA_USERD_OFFSET ||
        image->userd_relocation.width !=
            REIST_NVIDIA_GK208_ADDRESS_RELOCATION_WIDTH ||
        image->userd_relocation.reserved != 0U)
        return -84;

    for (uint32_t index = 0U;
         index < REIST_NVIDIA_GK208_RAMFC_WORDS; ++index)
        if (image->ramfc[index] != channel_ramfc_expected_word(index))
            return -84;
    if (image->ramfc[NVIDIA_GK208_RAMFC_USERD_LOW_WORD] != 0U ||
        image->ramfc[NVIDIA_GK208_RAMFC_USERD_HIGH_WORD] != 0U)
        return -84;
    for (uint32_t index = 0U;
         index < REIST_NVIDIA_GK208_USERD_WORDS; ++index)
        if (image->userd[index] != 0U) return -84;
    if (image->runlist[0] != REIST_NVIDIA_GK208_CHANNEL_ID ||
        image->runlist[1] != 0U)
        return -84;
    return 0;
}

int reist_nvidia_gk208_prepare_channel_image(
        reist_nvidia_gk208_channel_image_t *image) {
    if (image == NULL) return -22;
    for (uint32_t index = 0U;
         index < REIST_NVIDIA_GK208_RAMFC_WORDS; ++index)
        image->ramfc[index] = channel_ramfc_expected_word(index);
    for (uint32_t index = 0U;
         index < REIST_NVIDIA_GK208_USERD_WORDS; ++index)
        image->userd[index] = 0U;
    image->runlist[0] = REIST_NVIDIA_GK208_CHANNEL_ID;
    image->runlist[1] = 0U;
    image->ramfc_pool_offset = REIST_NVIDIA_GK208_DMA_RAMFC_OFFSET;
    image->ramfc_bytes = REIST_NVIDIA_GK208_RAMFC_BYTES;
    image->userd_pool_offset = REIST_NVIDIA_GK208_DMA_USERD_OFFSET;
    image->userd_bytes = REIST_NVIDIA_GK208_USERD_BYTES;
    image->runlist_pool_offset = REIST_NVIDIA_GK208_DMA_RUNLIST_OFFSET;
    image->runlist_bytes = REIST_NVIDIA_GK208_RUNLIST_BYTES;
    image->channel_id = REIST_NVIDIA_GK208_CHANNEL_ID;
    image->gpfifo_bytes = REIST_NVIDIA_GK208_GPFIFO_BYTES;
    image->userd_relocation = (reist_nvidia_gk208_address_relocation_t){
        .destination_pool_offset = REIST_NVIDIA_GK208_DMA_RAMFC_OFFSET + 0x08U,
        .source_pool_offset = REIST_NVIDIA_GK208_DMA_USERD_OFFSET,
        .width = REIST_NVIDIA_GK208_ADDRESS_RELOCATION_WIDTH,
    };
    return reist_nvidia_gk208_validate_channel_image(image);
}

#define NVIDIA_GK208_VM_ENTRY_BYTES 8U
#define NVIDIA_GK208_VM_INSTANCE_TARGET_NCOH 3ULL
#define NVIDIA_GK208_VM_PDE_TARGET_NCOH 3ULL
#define NVIDIA_GK208_VM_PTE_VALID 1ULL
#define NVIDIA_GK208_VM_PTE_READ_ONLY (1ULL << 2U)
#define NVIDIA_GK208_VM_PTE_APERTURE_NCOH (3ULL << 33U)

static uint32_t vm_table_bytes(uint32_t bits) {
    return (1U << bits) * NVIDIA_GK208_VM_ENTRY_BYTES;
}

static uint32_t vm_pgd_index(uint32_t pgt_bits) {
    return (uint32_t)(REIST_NVIDIA_GK208_PUSHBUF_GPU_ADDRESS >>
        (REIST_NVIDIA_GK208_GPU_PAGE_SHIFT + pgt_bits));
}

static reist_nvidia_gk208_vm_relocation_t vm_relocation(
        uint32_t destination, uint32_t source, uint32_t shift,
        uint64_t fixed_bits) {
    return (reist_nvidia_gk208_vm_relocation_t){
        .destination_pool_offset = destination,
        .source_pool_offset = source,
        .shift_right = shift,
        .width = REIST_NVIDIA_GK208_ADDRESS_RELOCATION_WIDTH,
        .fixed_bits = fixed_bits,
    };
}

static int vm_plan_geometry(uint32_t fb_page_shift, uint32_t *pgd_bits,
                            uint32_t *pgt_bits) {
    if (pgd_bits == NULL || pgt_bits == NULL) return -22;
    if (fb_page_shift == REIST_NVIDIA_GK208_FB_PAGE_SHIFT_64K) {
        *pgd_bits = 14U;
        *pgt_bits = 14U;
        return 0;
    }
    if (fb_page_shift == REIST_NVIDIA_GK208_FB_PAGE_SHIFT_128K) {
        *pgd_bits = 13U;
        *pgt_bits = 15U;
        return 0;
    }
    return -22;
}

static int build_vm_plan(
        reist_nvidia_gk208_vm_plan_t *plan, uint32_t fb_page_shift) {
    if (plan == NULL) return -22;
    uint32_t pgd_bits = 0U;
    uint32_t pgt_bits = 0U;
    if (vm_plan_geometry(fb_page_shift, &pgd_bits, &pgt_bits) != 0)
        return -22;
    const uint32_t pgd_index = vm_pgd_index(pgt_bits);
    *plan = (reist_nvidia_gk208_vm_plan_t){
        .fb_page_shift = fb_page_shift,
        .gpu_page_shift = REIST_NVIDIA_GK208_GPU_PAGE_SHIFT,
        .pgd_bits = pgd_bits,
        .pgt_bits = pgt_bits,
        .pgd_pool_offset = REIST_NVIDIA_GK208_DMA_PGD_OFFSET,
        .pgd_bytes = vm_table_bytes(pgd_bits),
        .pgt_pool_offset = REIST_NVIDIA_GK208_DMA_PGT_OFFSET,
        .pgt_bytes = vm_table_bytes(pgt_bits),
        .vm_limit = REIST_NVIDIA_GK208_VM_LIMIT,
        .relocation_count = REIST_NVIDIA_GK208_VM_RELOCATION_COUNT,
    };
    plan->relocations[0] = vm_relocation(
        REIST_NVIDIA_GK208_DMA_RAMFC_OFFSET +
            REIST_NVIDIA_GK208_RAMFC_PGD_OFFSET,
        plan->pgd_pool_offset, 0U, NVIDIA_GK208_VM_INSTANCE_TARGET_NCOH);
    plan->relocations[1] = vm_relocation(
        plan->pgd_pool_offset + pgd_index * NVIDIA_GK208_VM_ENTRY_BYTES,
        plan->pgt_pool_offset, 8U, NVIDIA_GK208_VM_PDE_TARGET_NCOH);
    const uint64_t pte_bits = NVIDIA_GK208_VM_PTE_VALID |
        NVIDIA_GK208_VM_PTE_APERTURE_NCOH;
    plan->relocations[2] = vm_relocation(
        plan->pgt_pool_offset, REIST_NVIDIA_GK208_DMA_PUSHBUF_OFFSET, 8U,
        pte_bits | NVIDIA_GK208_VM_PTE_READ_ONLY);
    plan->relocations[3] = vm_relocation(
        plan->pgt_pool_offset + NVIDIA_GK208_VM_ENTRY_BYTES,
        REIST_NVIDIA_GK208_DMA_FENCE_OFFSET, 8U, pte_bits);
    plan->relocations[4] = vm_relocation(
        plan->pgt_pool_offset + 2U * NVIDIA_GK208_VM_ENTRY_BYTES,
        REIST_NVIDIA_GK208_DMA_GPFIFO_OFFSET, 8U,
        pte_bits | NVIDIA_GK208_VM_PTE_READ_ONLY);
    return 0;
}

int reist_nvidia_gk208_prepare_vm_plan(
        reist_nvidia_gk208_vm_plan_t *plan, uint32_t fb_page_shift) {
    int status = build_vm_plan(plan, fb_page_shift);
    return status != 0 ? status : reist_nvidia_gk208_validate_vm_plan(plan);
}

static int vm_relocation_equal(
        const reist_nvidia_gk208_vm_relocation_t *left,
        const reist_nvidia_gk208_vm_relocation_t *right) {
    return left->destination_pool_offset == right->destination_pool_offset &&
        left->source_pool_offset == right->source_pool_offset &&
        left->shift_right == right->shift_right &&
        left->width == right->width && left->fixed_bits == right->fixed_bits;
}

int reist_nvidia_gk208_validate_vm_plan(
        const reist_nvidia_gk208_vm_plan_t *plan) {
    if (plan == NULL) return -22;
    uint32_t pgd_bits = 0U;
    uint32_t pgt_bits = 0U;
    if (vm_plan_geometry(plan->fb_page_shift, &pgd_bits, &pgt_bits) != 0 ||
        plan->gpu_page_shift != REIST_NVIDIA_GK208_GPU_PAGE_SHIFT ||
        plan->pgd_bits != pgd_bits || plan->pgt_bits != pgt_bits ||
        plan->pgd_bits + plan->pgt_bits + plan->gpu_page_shift !=
            REIST_NVIDIA_GK208_VM_ADDRESS_BITS ||
        plan->pgd_pool_offset != REIST_NVIDIA_GK208_DMA_PGD_OFFSET ||
        plan->pgd_bytes != vm_table_bytes(pgd_bits) ||
        plan->pgt_pool_offset != REIST_NVIDIA_GK208_DMA_PGT_OFFSET ||
        plan->pgt_bytes != vm_table_bytes(pgt_bits) ||
        plan->pgd_bytes > REIST_NVIDIA_GK208_DMA_PGD_RESERVATION_BYTES ||
        plan->pgt_bytes > REIST_NVIDIA_GK208_DMA_PGT_RESERVATION_BYTES ||
        plan->pgd_pool_offset +
                REIST_NVIDIA_GK208_DMA_PGD_RESERVATION_BYTES !=
            plan->pgt_pool_offset ||
        plan->pgt_pool_offset +
                REIST_NVIDIA_GK208_DMA_PGT_RESERVATION_BYTES >
            REIST_NVIDIA_GK208_DMA_POOL_BYTES ||
        plan->vm_limit != REIST_NVIDIA_GK208_VM_LIMIT ||
        plan->relocation_count != REIST_NVIDIA_GK208_VM_RELOCATION_COUNT ||
        plan->reserved != 0U)
        return -84;

    reist_nvidia_gk208_vm_plan_t expected;
    if (build_vm_plan(&expected, plan->fb_page_shift) != 0)
        return -84;
    for (uint32_t index = 0U;
         index < REIST_NVIDIA_GK208_VM_RELOCATION_COUNT; ++index) {
        const reist_nvidia_gk208_vm_relocation_t *relocation =
            &plan->relocations[index];
        if (!vm_relocation_equal(relocation, &expected.relocations[index]) ||
            relocation->width != NVIDIA_GK208_VM_ENTRY_BYTES ||
            (relocation->destination_pool_offset &
                (NVIDIA_GK208_VM_ENTRY_BYTES - 1U)) != 0U ||
            (relocation->source_pool_offset &
                ((1U << REIST_NVIDIA_GK208_GPU_PAGE_SHIFT) - 1U)) != 0U ||
            relocation->destination_pool_offset >
                REIST_NVIDIA_GK208_DMA_POOL_BYTES - relocation->width ||
            relocation->source_pool_offset >=
                REIST_NVIDIA_GK208_DMA_POOL_BYTES)
            return -84;
    }
    return 0;
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

int reist_nvidia_gk208_submission_self_test(void) {
    reist_nvidia_gk208_pushbuf_t commands;
    reist_nvidia_gk208_submission_t submission;
    const reist_nvidia_gk208_surface_t surface = {
        .gpu_address = 0x10000000ULL,
        .width = 1024U,
        .height = 768U,
        .pitch = 4096U,
    };
    const reist_nvidia_gk208_rect_t rect = {8U, 8U, 16U, 16U};
    const uint64_t push_address = 0x0000000020000000ULL;
    const uint64_t fence_address = 0x0000000020001000ULL;
    if (reist_nvidia_gk208_encode_fill(
            &commands, &surface, &rect, 0x00010203U) != 0 ||
        reist_nvidia_gk208_prepare_submission(
            &submission, &commands, push_address, fence_address, 1U) != 0 ||
        reist_nvidia_gk208_validate_submission(
            &submission, push_address, fence_address, 1U) != 0)
        return -84;
    submission.gpfifo_entry[1] |= 1U << 8U;
    return reist_nvidia_gk208_validate_submission(
        &submission, push_address, fence_address, 1U) == -84 ? 0 : -84;
}

int reist_nvidia_gk208_dma_staging_self_test(void) {
    reist_nvidia_gk208_pushbuf_t commands;
    reist_nvidia_gk208_submission_t submission;
    reist_nvidia_gk208_dma_staging_t staging;
    const reist_nvidia_gk208_surface_t surface = {
        .gpu_address = 0x10000000ULL,
        .width = 1024U,
        .height = 768U,
        .pitch = 4096U,
    };
    const reist_nvidia_gk208_rect_t rect = {4U, 4U, 8U, 8U};
    if (reist_nvidia_gk208_encode_fill(
            &commands, &surface, &rect, 0x00010203U) != 0 ||
        reist_nvidia_gk208_prepare_submission(
            &submission, &commands,
            REIST_NVIDIA_GK208_PUSHBUF_GPU_ADDRESS,
            REIST_NVIDIA_GK208_FENCE_GPU_ADDRESS, 1U) != 0 ||
        reist_nvidia_gk208_prepare_dma_staging(
            &staging, &submission, 1U) != 0)
        return -84;
    staging.pushbuf_offset = staging.gpfifo_offset;
    return reist_nvidia_gk208_validate_dma_staging(
        &staging, &submission, 1U) == -84 ? 0 : -84;
}

int reist_nvidia_gk208_channel_image_self_test(void) {
    reist_nvidia_gk208_channel_image_t image;
    if (reist_nvidia_gk208_prepare_channel_image(&image) != 0 ||
        reist_nvidia_gk208_validate_channel_image(&image) != 0)
        return -84;
    image.ramfc[0x14U / sizeof(uint32_t)] = 1U;
    if (reist_nvidia_gk208_validate_channel_image(&image) != -84)
        return -84;
    if (reist_nvidia_gk208_prepare_channel_image(&image) != 0)
        return -84;
    image.userd_relocation.source_pool_offset += 0x1000U;
    return reist_nvidia_gk208_validate_channel_image(&image) == -84
        ? 0 : -84;
}

int reist_nvidia_gk208_vm_plan_self_test(void) {
    reist_nvidia_gk208_vm_plan_t plan;
    if (reist_nvidia_gk208_prepare_vm_plan(
            &plan, REIST_NVIDIA_GK208_FB_PAGE_SHIFT_64K) != 0 ||
        reist_nvidia_gk208_validate_vm_plan(&plan) != 0)
        return -84;
    plan.relocations[3].fixed_bits |= NVIDIA_GK208_VM_PTE_READ_ONLY;
    if (reist_nvidia_gk208_validate_vm_plan(&plan) != -84)
        return -84;
    if (reist_nvidia_gk208_prepare_vm_plan(
            &plan, REIST_NVIDIA_GK208_FB_PAGE_SHIFT_128K) != 0 ||
        reist_nvidia_gk208_validate_vm_plan(&plan) != 0)
        return -84;
    ++plan.pgt_bytes;
    return reist_nvidia_gk208_validate_vm_plan(&plan) == -84 ? 0 : -84;
}

static int gr_firmware_image(uint32_t component, uint32_t section,
                             const uint32_t **words_out,
                             uint32_t *word_count_out) {
    if (words_out == NULL || word_count_out == NULL) return -22;
    *words_out = NULL;
    *word_count_out = 0U;
    if (component == REIST_NVIDIA_GK208_GR_COMPONENT_FECS) {
        if (section == REIST_NVIDIA_GK208_GR_SECTION_DATA) {
            *words_out = gk208_grhub_data;
            *word_count_out = REIST_NVIDIA_GK208_GR_FECS_DATA_WORDS;
            return 0;
        }
        if (section == REIST_NVIDIA_GK208_GR_SECTION_CODE) {
            *words_out = gk208_grhub_code;
            *word_count_out = REIST_NVIDIA_GK208_GR_FECS_CODE_WORDS;
            return 0;
        }
        return -34;
    }
    if (component == REIST_NVIDIA_GK208_GR_COMPONENT_GPCCS) {
        if (section == REIST_NVIDIA_GK208_GR_SECTION_DATA) {
            *words_out = gk208_grgpc_data;
            *word_count_out = REIST_NVIDIA_GK208_GR_GPCCS_DATA_WORDS;
            return 0;
        }
        if (section == REIST_NVIDIA_GK208_GR_SECTION_CODE) {
            *words_out = gk208_grgpc_code;
            *word_count_out = REIST_NVIDIA_GK208_GR_GPCCS_CODE_WORDS;
            return 0;
        }
        return -34;
    }
    return -34;
}

static uint32_t gr_firmware_crc32(const uint32_t *words,
                                  uint32_t word_count) {
    uint32_t crc = UINT32_MAX;
    for (uint32_t index = 0U; index < word_count; ++index) {
        for (uint32_t shift = 0U; shift < 32U; shift += 8U) {
            crc ^= (words[index] >> shift) & 0xFFU;
            for (uint32_t bit = 0U; bit < 8U; ++bit) {
                uint32_t polynomial_mask = 0U - (crc & 1U);
                crc = (crc >> 1U) ^ (0xEDB88320U & polynomial_mask);
            }
        }
    }
    return ~crc;
}

int reist_nvidia_gk208_gr_firmware_manifest(
    reist_nvidia_gk208_gr_firmware_manifest_t *manifest) {
    if (manifest == NULL) return -22;
    *manifest = (reist_nvidia_gk208_gr_firmware_manifest_t){
        .version = REIST_NVIDIA_GK208_GR_FIRMWARE_MANIFEST_VERSION,
        .struct_size = sizeof(*manifest),
        .fecs_data_words = REIST_NVIDIA_GK208_GR_FECS_DATA_WORDS,
        .fecs_code_words = REIST_NVIDIA_GK208_GR_FECS_CODE_WORDS,
        .gpccs_data_words = REIST_NVIDIA_GK208_GR_GPCCS_DATA_WORDS,
        .gpccs_code_words = REIST_NVIDIA_GK208_GR_GPCCS_CODE_WORDS,
        .fecs_data_crc32 = REIST_NVIDIA_GK208_GR_FECS_DATA_CRC32,
        .fecs_code_crc32 = REIST_NVIDIA_GK208_GR_FECS_CODE_CRC32,
        .gpccs_data_crc32 = REIST_NVIDIA_GK208_GR_GPCCS_DATA_CRC32,
        .gpccs_code_crc32 = REIST_NVIDIA_GK208_GR_GPCCS_CODE_CRC32,
        .total_words = REIST_NVIDIA_GK208_GR_FIRMWARE_TOTAL_WORDS,
    };
    return 0;
}

int reist_nvidia_gk208_gr_firmware_word(
    uint32_t component, uint32_t section, uint32_t index,
    uint32_t *word_out) {
    if (word_out == NULL) return -22;
    const uint32_t *words = NULL;
    uint32_t word_count = 0U;
    int status = gr_firmware_image(
        component, section, &words, &word_count);
    if (status != 0 || index >= word_count)
        return status != 0 ? status : -34;
    *word_out = words[index];
    return 0;
}

int reist_nvidia_gk208_gr_firmware_self_test(void) {
    reist_nvidia_gk208_gr_firmware_manifest_t manifest;
    if (reist_nvidia_gk208_gr_firmware_manifest(&manifest) != 0 ||
        manifest.version != REIST_NVIDIA_GK208_GR_FIRMWARE_MANIFEST_VERSION ||
        manifest.struct_size != sizeof(manifest) ||
        manifest.fecs_data_words != REIST_NVIDIA_GK208_GR_FECS_DATA_WORDS ||
        manifest.fecs_code_words != REIST_NVIDIA_GK208_GR_FECS_CODE_WORDS ||
        manifest.gpccs_data_words != REIST_NVIDIA_GK208_GR_GPCCS_DATA_WORDS ||
        manifest.gpccs_code_words != REIST_NVIDIA_GK208_GR_GPCCS_CODE_WORDS ||
        manifest.total_words != REIST_NVIDIA_GK208_GR_FIRMWARE_TOTAL_WORDS ||
        gr_firmware_crc32(gk208_grhub_data,
            REIST_NVIDIA_GK208_GR_FECS_DATA_WORDS) !=
                manifest.fecs_data_crc32 ||
        gr_firmware_crc32(gk208_grhub_code,
            REIST_NVIDIA_GK208_GR_FECS_CODE_WORDS) !=
                manifest.fecs_code_crc32 ||
        gr_firmware_crc32(gk208_grgpc_data,
            REIST_NVIDIA_GK208_GR_GPCCS_DATA_WORDS) !=
                manifest.gpccs_data_crc32 ||
        gr_firmware_crc32(gk208_grgpc_code,
            REIST_NVIDIA_GK208_GR_GPCCS_CODE_WORDS) !=
                manifest.gpccs_code_crc32)
        return -84;
    for (uint32_t index = 0U;
         index < sizeof(manifest.reserved) / sizeof(manifest.reserved[0]);
         ++index)
        if (manifest.reserved[index] != 0U) return -84;

    uint32_t word = 0U;
    if (reist_nvidia_gk208_gr_firmware_word(
            REIST_NVIDIA_GK208_GR_COMPONENT_FECS,
            REIST_NVIDIA_GK208_GR_SECTION_DATA, 0U, &word) != 0 ||
        word != gk208_grhub_data[0] ||
        reist_nvidia_gk208_gr_firmware_word(
            REIST_NVIDIA_GK208_GR_COMPONENT_FECS,
            REIST_NVIDIA_GK208_GR_SECTION_CODE,
            REIST_NVIDIA_GK208_GR_FECS_CODE_WORDS - 1U, &word) != 0 ||
        word != gk208_grhub_code[REIST_NVIDIA_GK208_GR_FECS_CODE_WORDS - 1U] ||
        reist_nvidia_gk208_gr_firmware_word(
            REIST_NVIDIA_GK208_GR_COMPONENT_GPCCS,
            REIST_NVIDIA_GK208_GR_SECTION_DATA, 0U, &word) != 0 ||
        word != gk208_grgpc_data[0] ||
        reist_nvidia_gk208_gr_firmware_word(
            REIST_NVIDIA_GK208_GR_COMPONENT_GPCCS,
            REIST_NVIDIA_GK208_GR_SECTION_CODE,
            REIST_NVIDIA_GK208_GR_GPCCS_CODE_WORDS - 1U, &word) != 0 ||
        word != gk208_grgpc_code[
            REIST_NVIDIA_GK208_GR_GPCCS_CODE_WORDS - 1U] ||
        reist_nvidia_gk208_gr_firmware_word(0U,
            REIST_NVIDIA_GK208_GR_SECTION_DATA, 0U, &word) != -34 ||
        reist_nvidia_gk208_gr_firmware_word(
            REIST_NVIDIA_GK208_GR_COMPONENT_FECS, 0U, 0U, &word) != -34 ||
        reist_nvidia_gk208_gr_firmware_word(
            REIST_NVIDIA_GK208_GR_COMPONENT_FECS,
            REIST_NVIDIA_GK208_GR_SECTION_DATA,
            REIST_NVIDIA_GK208_GR_FECS_DATA_WORDS, &word) != -34 ||
        reist_nvidia_gk208_gr_firmware_word(
            REIST_NVIDIA_GK208_GR_COMPONENT_FECS,
            REIST_NVIDIA_GK208_GR_SECTION_DATA, 0U, NULL) != -22)
        return -84;
    return 0;
}

static uint32_t gr_table_crc32(
    const reist_nvidia_gk208_gr_tuple_t *tuples, uint32_t tuple_count) {
    uint32_t crc = UINT32_MAX;
    for (uint32_t index = 0U; index < tuple_count; ++index) {
        const uint32_t fields[4] = {
            tuples[index].address, tuples[index].count,
            tuples[index].pitch, tuples[index].value,
        };
        for (uint32_t field = 0U; field < 4U; ++field) {
            for (uint32_t shift = 0U; shift < 32U; shift += 8U) {
                crc ^= (fields[field] >> shift) & 0xFFU;
                for (uint32_t bit = 0U; bit < 8U; ++bit) {
                    const uint32_t mask = 0U - (crc & 1U);
                    crc = (crc >> 1U) ^ (0xEDB88320U & mask);
                }
            }
        }
    }
    return ~crc;
}

static int gr_tuple_valid(const reist_nvidia_gk208_gr_tuple_t *tuple) {
    if (tuple == NULL || tuple->count == 0U || tuple->count > 255U ||
        (tuple->address & 3U) != 0U || tuple->pitch == 0U ||
        (tuple->pitch & 3U) != 0U)
        return 0;
    const uint64_t last = (uint64_t)tuple->address +
        (uint64_t)(tuple->count - 1U) * tuple->pitch;
    return last <= 0x007FFFFCU;
}

static int gr_icmd_tuple_valid(
        const reist_nvidia_gk208_gr_tuple_t *tuple) {
    if (tuple == NULL || tuple->count == 0U || tuple->count > 255U ||
        tuple->pitch != 1U)
        return 0;
    const uint64_t last = (uint64_t)tuple->address + tuple->count - 1U;
    return last <= 0x001FFFFFULL;
}

int reist_nvidia_gk208_gr_plan_manifest(
    reist_nvidia_gk208_gr_plan_manifest_t *manifest) {
    if (manifest == NULL) return -22;
    *manifest = (reist_nvidia_gk208_gr_plan_manifest_t){
        .version = REIST_NVIDIA_GK208_GR_PLAN_VERSION,
        .struct_size = sizeof(*manifest),
        .mmio_pack_count = REIST_GK208_GR_MMIO_PACK_COUNT,
        .mmio_tuple_count = REIST_GK208_GR_MMIO_TUPLE_COUNT,
        .mmio_crc32 = REIST_GK208_GR_MMIO_CRC32,
        .context_pack_count = REIST_GK208_GR_CONTEXT_PACK_COUNT,
        .context_tuple_count = REIST_GK208_GR_CONTEXT_TUPLE_COUNT,
        .context_crc32 = REIST_GK208_GR_CONTEXT_CRC32,
        .hub_command_offset = 0x0040910CU,
        .hub_command_value = 0x00000000U,
        .hub_start_offset = 0x00409100U,
        .hub_start_value = 0x00000002U,
        .ready_offset = 0x00409800U,
        .ready_mask = 0x80000000U,
        .context_size_offset = 0x00409804U,
        .ready_deadline_ms = 2000U,
    };
    return 0;
}

int reist_nvidia_gk208_gr_mmio_tuple(
    uint32_t pack, uint32_t index, reist_nvidia_gk208_gr_tuple_t *tuple) {
    if (tuple == NULL) return -22;
    if (pack >= REIST_GK208_GR_MMIO_PACK_COUNT) return -34;
    const reist_nvidia_gk208_gr_span_t *span = &reist_gk208_gr_mmio_spans[pack];
    if (index >= span->tuple_count) return -34;
    *tuple = reist_gk208_gr_mmio[span->first_tuple + index];
    return 0;
}

int reist_nvidia_gk208_gr_context_tuple(
    uint32_t pack, uint32_t index, reist_nvidia_gk208_gr_tuple_t *tuple) {
    if (tuple == NULL) return -22;
    if (pack >= REIST_GK208_GR_CONTEXT_PACK_COUNT) return -34;
    const reist_nvidia_gk208_gr_context_span_t *span =
        &reist_gk208_gr_context_spans[pack];
    if (index >= span->tuple_count) return -34;
    *tuple = reist_gk208_gr_context[span->first_tuple + index];
    return 0;
}

int reist_nvidia_gk208_gr_validate_topology(
    const reist_nvidia_gk208_gr_topology_t *topology) {
    if (topology == NULL) return -22;
    if (topology->version != REIST_NVIDIA_GK208_GR_PLAN_VERSION ||
        topology->struct_size != sizeof(*topology) ||
        topology->gpc_count == 0U ||
        topology->gpc_count > REIST_NVIDIA_GK208_MAX_GPCS ||
        topology->rop_count == 0U ||
        topology->rop_count > REIST_NVIDIA_GK208_MAX_ROPS ||
        topology->tpc_total == 0U ||
        topology->tpc_total > REIST_NVIDIA_GK208_MAX_TOTAL_TPCS ||
        topology->tpc_max == 0U ||
        topology->tpc_max > REIST_NVIDIA_GK208_MAX_TPCS_PER_GPC ||
        topology->reserved[0] != 0U || topology->reserved[1] != 0U)
        return -84;
    uint32_t total = 0U;
    uint32_t maximum = 0U;
    for (uint32_t gpc = 0U; gpc < REIST_NVIDIA_GK208_MAX_GPCS; ++gpc) {
        const uint32_t tpcs = topology->tpc_count[gpc];
        const uint32_t mask = topology->ppc_tpc_mask[gpc];
        if (gpc >= topology->gpc_count) {
            if (tpcs != 0U || mask != 0U) return -84;
            continue;
        }
        if (tpcs == 0U || tpcs > REIST_NVIDIA_GK208_MAX_TPCS_PER_GPC ||
            mask == 0U || (mask >> tpcs) != 0U)
            return -84;
        uint32_t bits = 0U;
        for (uint32_t value = mask; value != 0U; value >>= 1U)
            bits += value & 1U;
        if (bits != tpcs || total > REIST_NVIDIA_GK208_MAX_TOTAL_TPCS - tpcs)
            return -84;
        total += tpcs;
        if (maximum < tpcs) maximum = tpcs;
    }
    return total == topology->tpc_total && maximum == topology->tpc_max
        ? 0 : -84;
}

static int gr_context_emit(reist_nvidia_gk208_gr_context_plan_t *plan,
                           uint32_t address, uint32_t transfer_count) {
    if (transfer_count == 0U || transfer_count > 32U ||
        address > 0x03FFFFFFU ||
        plan->word_count >= REIST_NVIDIA_GK208_GR_CONTEXT_TRANSFER_CAPACITY)
        return -84;
    plan->words[plan->word_count++] =
        ((transfer_count - 1U) << 26U) | address;
    return 0;
}

int reist_nvidia_gk208_gr_compile_context_plan(
    reist_nvidia_gk208_gr_context_plan_t *plan) {
    if (plan == NULL) return -22;
    plan->version = REIST_NVIDIA_GK208_GR_PLAN_VERSION;
    plan->struct_size = sizeof(*plan);
    plan->word_count = 0U;
    for (uint32_t group = 0U;
         group < REIST_GK208_GR_CONTEXT_PACK_COUNT; ++group) {
        const reist_nvidia_gk208_gr_context_span_t *span =
            &reist_gk208_gr_context_spans[group];
        uint32_t start = 0U;
        uint32_t previous = UINT32_MAX;
        uint32_t transfers = 0U;
        plan->group_first[group] = plan->word_count;
        for (uint32_t index = 0U; index < span->tuple_count; ++index) {
            const reist_nvidia_gk208_gr_tuple_t *tuple =
                &reist_gk208_gr_context[span->first_tuple + index];
            if (!gr_tuple_valid(tuple) || tuple->address < span->register_base)
                return -84;
            uint32_t head = tuple->address - span->register_base;
            for (uint32_t item = 0U; item < tuple->count; ++item) {
                if (head != previous + 4U || transfers >= 32U) {
                    if (transfers != 0U &&
                        gr_context_emit(plan, start, transfers) != 0)
                        return -84;
                    start = head;
                    transfers = 0U;
                }
                previous = head;
                ++transfers;
                if (item + 1U < tuple->count &&
                    head > UINT32_MAX - tuple->pitch)
                    return -84;
                head += tuple->pitch;
            }
        }
        if (gr_context_emit(plan, start, transfers) != 0) return -84;
        plan->group_count[group] =
            plan->word_count - plan->group_first[group];
    }
    return 0;
}

int reist_nvidia_gk208_gr_validate_context_plan(
    const reist_nvidia_gk208_gr_context_plan_t *plan) {
    if (plan == NULL) return -22;
    if (plan->version != REIST_NVIDIA_GK208_GR_PLAN_VERSION ||
        plan->struct_size != sizeof(*plan) || plan->word_count == 0U ||
        plan->word_count > REIST_NVIDIA_GK208_GR_CONTEXT_TRANSFER_CAPACITY)
        return -84;
    reist_nvidia_gk208_gr_context_plan_t expected;
    if (reist_nvidia_gk208_gr_compile_context_plan(&expected) != 0 ||
        expected.word_count != plan->word_count)
        return -84;
    for (uint32_t group = 0U;
         group < REIST_NVIDIA_GK208_GR_CONTEXT_PACK_COUNT; ++group)
        if (expected.group_first[group] != plan->group_first[group] ||
            expected.group_count[group] != plan->group_count[group])
            return -84;
    for (uint32_t index = 0U; index < plan->word_count; ++index)
        if (expected.words[index] != plan->words[index]) return -84;
    return 0;
}

int reist_nvidia_gk208_gr_plan_self_test(void) {
    reist_nvidia_gk208_gr_plan_manifest_t manifest;
    if (reist_nvidia_gk208_gr_plan_manifest(&manifest) != 0 ||
        manifest.version != REIST_NVIDIA_GK208_GR_PLAN_VERSION ||
        manifest.struct_size != sizeof(manifest) ||
        manifest.mmio_pack_count != REIST_GK208_GR_MMIO_PACK_COUNT ||
        manifest.mmio_tuple_count != REIST_GK208_GR_MMIO_TUPLE_COUNT ||
        manifest.context_pack_count != REIST_GK208_GR_CONTEXT_PACK_COUNT ||
        manifest.context_tuple_count != REIST_GK208_GR_CONTEXT_TUPLE_COUNT ||
        gr_table_crc32(reist_gk208_gr_mmio,
            REIST_GK208_GR_MMIO_TUPLE_COUNT) != manifest.mmio_crc32 ||
        gr_table_crc32(reist_gk208_gr_context,
            REIST_GK208_GR_CONTEXT_TUPLE_COUNT) != manifest.context_crc32 ||
        manifest.hub_command_offset != 0x0040910CU ||
        manifest.hub_command_value != 0U ||
        manifest.hub_start_offset != 0x00409100U ||
        manifest.hub_start_value != 2U ||
        manifest.ready_offset != 0x00409800U ||
        manifest.ready_mask != 0x80000000U ||
        manifest.context_size_offset != 0x00409804U ||
        manifest.ready_deadline_ms != 2000U)
        return -84;
    for (uint32_t index = 0U; index < REIST_GK208_GR_MMIO_TUPLE_COUNT;
         ++index)
        if (!gr_tuple_valid(&reist_gk208_gr_mmio[index])) return -84;
    for (uint32_t index = 0U; index < REIST_GK208_GR_CONTEXT_TUPLE_COUNT;
         ++index)
        if (!gr_tuple_valid(&reist_gk208_gr_context[index])) return -84;

    reist_nvidia_gk208_gr_context_plan_t plan;
    if (reist_nvidia_gk208_gr_compile_context_plan(&plan) != 0 ||
        reist_nvidia_gk208_gr_validate_context_plan(&plan) != 0)
        return -84;
    reist_nvidia_gk208_gr_tuple_t tuple;
    if (reist_nvidia_gk208_gr_mmio_tuple(0U, 0U, &tuple) != 0 ||
        tuple.address != 0x00400080U ||
        reist_nvidia_gk208_gr_context_tuple(0U, 0U, &tuple) != 0 ||
        tuple.address != 0x00400204U ||
        reist_nvidia_gk208_gr_mmio_tuple(
            REIST_GK208_GR_MMIO_PACK_COUNT, 0U, &tuple) != -34 ||
        reist_nvidia_gk208_gr_context_tuple(0U,
            reist_gk208_gr_context_spans[0].tuple_count, &tuple) != -34)
        return -84;

    reist_nvidia_gk208_gr_topology_t topology;
    topology.version = REIST_NVIDIA_GK208_GR_PLAN_VERSION;
    topology.struct_size = sizeof(topology);
    topology.gpc_count = 1U;
    topology.rop_count = 2U;
    topology.tpc_total = 2U;
    topology.tpc_max = 2U;
    for (uint32_t index = 0U; index < REIST_NVIDIA_GK208_MAX_GPCS;
         ++index) {
        topology.tpc_count[index] = 0U;
        topology.ppc_tpc_mask[index] = 0U;
    }
    topology.tpc_count[0] = 2U;
    topology.ppc_tpc_mask[0] = 3U;
    topology.reserved[0] = 0U;
    topology.reserved[1] = 0U;
    if (reist_nvidia_gk208_gr_validate_topology(&topology) != 0)
        return -84;
    topology.ppc_tpc_mask[0] = 1U;
    if (reist_nvidia_gk208_gr_validate_topology(&topology) != -84)
        return -84;
    return 0;
}

/*
 * Hardware-inactive execution image for the GK208 nofw path pinned to Linux
 * Nouveau commit 45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229 (gf100/gf117/
 * gk104/gk208 GR and gf100/gk104 LTC implementations).
 * The operation ABI is intentionally semantic where a future kernel mediator
 * must perform a read/modify/write, wait, context-list transaction or resolve
 * one of the two device-VRAM offsets.  Ring 3 never substitutes an address.
 */
#define GK208_GR_GPC_BCAST_BASE 0x00418000U
#define GK208_GR_TPC_UNIT_BASE 0x00504000U
#define GK208_GR_PPC_UNIT_BASE 0x00503000U
#define GK208_GR_ROP_UNIT_BASE 0x00410000U
#define GK208_GR_TPC_UNIT_STRIDE 0x00000800U
#define GK208_GR_PPC_UNIT_STRIDE 0x00000200U
#define GK208_GR_ROP_UNIT_STRIDE 0x00000400U
#define GK208_GR_LTC_ZBC_INDEX 0x0017EA44U
#define GK208_GR_LTC_ZBC_COLOR 0x0017EA48U
#define GK208_GR_LTC_ZBC_DEPTH 0x0017EA58U
#define GK208_GR_ZBC_SLOT_MIN 1U
#define GK208_GR_ZBC_SLOT_MAX 15U
#define GK208_GR_ZCULL_TILE_SLOTS 32U

enum {
    GK208_GR_EXECUTION_SECTION_GENERAL = 0U,
    GK208_GR_EXECUTION_SECTION_STATIC = 1U,
    GK208_GR_EXECUTION_SECTION_ZBC = 2U,
    GK208_GR_EXECUTION_SECTION_CONTEXT = 3U,
};

typedef struct {
    reist_nvidia_gk208_gr_execution_image_t *output;
    const reist_nvidia_gk208_gr_execution_image_t *expected;
    uint32_t operation_count;
    uint32_t static_count;
    uint32_t zbc_count;
    uint32_t context_count;
    uint32_t vram_count;
    uint32_t section;
} gk208_gr_execution_builder_t;

static uint32_t gr_crc32_word(uint32_t crc, uint32_t word) {
    for (uint32_t shift = 0U; shift < 32U; shift += 8U) {
        crc ^= (word >> shift) & 0xFFU;
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t polynomial_mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & polynomial_mask);
        }
    }
    return crc;
}

static uint32_t gr_execution_operation_crc32(
    const reist_nvidia_gk208_gr_execution_op_t *operations,
    uint32_t operation_count) {
    uint32_t crc = UINT32_MAX;
    for (uint32_t index = 0U; index < operation_count; ++index) {
        crc = gr_crc32_word(crc, operations[index].opcode);
        crc = gr_crc32_word(crc, operations[index].address);
        crc = gr_crc32_word(crc, operations[index].value);
        crc = gr_crc32_word(crc, operations[index].mask);
    }
    return ~crc;
}

static uint32_t gr_execution_topology_crc32(
    const reist_nvidia_gk208_gr_topology_t *topology) {
    uint32_t crc = UINT32_MAX;
    crc = gr_crc32_word(crc, topology->version);
    crc = gr_crc32_word(crc, topology->struct_size);
    crc = gr_crc32_word(crc, topology->gpc_count);
    crc = gr_crc32_word(crc, topology->rop_count);
    crc = gr_crc32_word(crc, topology->tpc_total);
    crc = gr_crc32_word(crc, topology->tpc_max);
    for (uint32_t index = 0U; index < REIST_NVIDIA_GK208_MAX_GPCS;
         ++index)
        crc = gr_crc32_word(crc, topology->tpc_count[index]);
    for (uint32_t index = 0U; index < REIST_NVIDIA_GK208_MAX_GPCS;
         ++index)
        crc = gr_crc32_word(crc, topology->ppc_tpc_mask[index]);
    crc = gr_crc32_word(crc, topology->reserved[0]);
    crc = gr_crc32_word(crc, topology->reserved[1]);
    return ~crc;
}

static int gr_context_align_up(uint32_t value, uint32_t alignment,
                               uint32_t *result) {
    if (result == NULL || alignment == 0U ||
        (alignment & (alignment - 1U)) != 0U ||
        value > UINT32_MAX - (alignment - 1U))
        return -84;
    *result = (value + alignment - 1U) & ~(alignment - 1U);
    return 0;
}

static int gr_context_memory_build(
        reist_nvidia_gk208_gr_context_memory_plan_t *plan,
        const reist_nvidia_gk208_gr_topology_t *topology,
        uint32_t context_size) {
    if (plan == NULL || topology == NULL || context_size == 0U ||
        reist_nvidia_gk208_gr_validate_topology(topology) != 0)
        return -22;
    const uint64_t attrib =
        (uint64_t)REIST_NVIDIA_GK208_GR_ATTRIB_STRIDE *
        (REIST_NVIDIA_GK208_GR_ATTRIB_NR_MAX +
         REIST_NVIDIA_GK208_GR_ALPHA_NR_MAX) * topology->tpc_total;
    uint32_t aligned_context = 0U;
    if (attrib == 0U || attrib > UINT32_MAX ||
        gr_context_align_up(context_size,
            REIST_NVIDIA_GK208_GR_GOLDEN_ALIGNMENT,
            &aligned_context) != 0 ||
        aligned_context > UINT32_MAX -
            REIST_NVIDIA_GK208_GR_GOLDEN_CB_RESERVED)
        return -84;
    const uint32_t golden =
        REIST_NVIDIA_GK208_GR_GOLDEN_CB_RESERVED + aligned_context;
    uint32_t cursor = 0U;
    if (gr_context_align_up(cursor,
            REIST_NVIDIA_GK208_GR_PAGEPOOL_ALIGNMENT, &cursor) != 0 ||
        cursor > UINT32_MAX - REIST_NVIDIA_GK208_GR_PAGEPOOL_BYTES)
        return -84;
    cursor += REIST_NVIDIA_GK208_GR_PAGEPOOL_BYTES;
    if (gr_context_align_up(cursor,
            REIST_NVIDIA_GK208_GR_BUNDLE_ALIGNMENT, &cursor) != 0 ||
        cursor > UINT32_MAX - REIST_NVIDIA_GK208_GR_BUNDLE_BYTES)
        return -84;
    cursor += REIST_NVIDIA_GK208_GR_BUNDLE_BYTES;
    if (gr_context_align_up(cursor,
            REIST_NVIDIA_GK208_GR_ATTRIB_ALIGNMENT, &cursor) != 0 ||
        cursor > UINT32_MAX - (uint32_t)attrib)
        return -84;
    cursor += (uint32_t)attrib;
    if (gr_context_align_up(cursor,
            REIST_NVIDIA_GK208_GR_GOLDEN_ALIGNMENT, &cursor) != 0 ||
        cursor > UINT32_MAX - golden)
        return -84;
    cursor += golden;
    *plan = (reist_nvidia_gk208_gr_context_memory_plan_t){
        .version = REIST_NVIDIA_GK208_GR_CONTEXT_MEMORY_PLAN_VERSION,
        .struct_size = sizeof(*plan),
        .topology_crc32 = gr_execution_topology_crc32(topology),
        .tpc_total = topology->tpc_total,
        .context_size = context_size,
        .pagepool_bytes = REIST_NVIDIA_GK208_GR_PAGEPOOL_BYTES,
        .pagepool_alignment = REIST_NVIDIA_GK208_GR_PAGEPOOL_ALIGNMENT,
        .bundle_bytes = REIST_NVIDIA_GK208_GR_BUNDLE_BYTES,
        .bundle_alignment = REIST_NVIDIA_GK208_GR_BUNDLE_ALIGNMENT,
        .attrib_bytes = (uint32_t)attrib,
        .attrib_alignment = REIST_NVIDIA_GK208_GR_ATTRIB_ALIGNMENT,
        .golden_cb_reserved =
            REIST_NVIDIA_GK208_GR_GOLDEN_CB_RESERVED,
        .golden_bytes = golden,
        .golden_alignment = REIST_NVIDIA_GK208_GR_GOLDEN_ALIGNMENT,
        .total_bytes = cursor,
    };
    return 0;
}

int reist_nvidia_gk208_gr_compile_context_memory_plan(
        reist_nvidia_gk208_gr_context_memory_plan_t *plan,
        const reist_nvidia_gk208_gr_topology_t *topology,
        uint32_t context_size) {
    int status = gr_context_memory_build(plan, topology, context_size);
    return status != 0 ? status :
        reist_nvidia_gk208_gr_validate_context_memory_plan(
            plan, topology, context_size);
}

int reist_nvidia_gk208_gr_validate_context_memory_plan(
        const reist_nvidia_gk208_gr_context_memory_plan_t *plan,
        const reist_nvidia_gk208_gr_topology_t *topology,
        uint32_t context_size) {
    if (plan == NULL || topology == NULL) return -22;
    reist_nvidia_gk208_gr_context_memory_plan_t expected;
    if (gr_context_memory_build(&expected, topology, context_size) != 0)
        return -84;
    const uint32_t *actual_words = (const uint32_t *)plan;
    const uint32_t *expected_words = (const uint32_t *)&expected;
    for (uint32_t index = 0U; index < sizeof(expected) / sizeof(uint32_t);
         ++index)
        if (actual_words[index] != expected_words[index]) return -84;
    return 0;
}

static uint32_t gr_method_table_crc32(void) {
    uint32_t crc = UINT32_MAX;
    for (uint32_t index = 0U; index < REIST_GK208_GR_MTHD_TUPLE_COUNT;
         ++index) {
        const reist_nvidia_gk208_gr_method_tuple_t *method =
            &reist_gk208_gr_mthd[index];
        const uint32_t fields[5] = {
            method->tuple.address, method->tuple.count,
            method->tuple.pitch, method->tuple.value, method->class_id,
        };
        for (uint32_t field = 0U; field < 5U; ++field) {
            for (uint32_t shift = 0U; shift < 32U; shift += 8U) {
                crc ^= (fields[field] >> shift) & 0xFFU;
                for (uint32_t bit = 0U; bit < 8U; ++bit) {
                    const uint32_t mask = 0U - (crc & 1U);
                    crc = (crc >> 1U) ^ (0xEDB88320U & mask);
                }
            }
        }
    }
    return ~crc;
}

static int gr_golden_emit_patch(
        reist_nvidia_gk208_gr_golden_plan_t *plan, uint32_t opcode,
        uint32_t address, uint32_t value) {
    if (plan == NULL ||
        (opcode != REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_WRITE32 &&
         opcode != REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_COPY32) ||
        (address & 3U) != 0U || address > 0x007FFFFCU ||
        plan->patch_count >= REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_CAPACITY)
        return -84;
    if (opcode == REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_COPY32 &&
        ((value & 3U) != 0U || value > 0x007FFFFCU))
        return -84;
    plan->patches[plan->patch_count++] =
        (reist_nvidia_gk208_gr_golden_patch_t){
            .opcode = opcode,
            .address = address,
            .value = value,
        };
    return 0;
}

static int gr_golden_add_map(
        reist_nvidia_gk208_gr_golden_plan_t *plan, uint32_t index,
        uint32_t buffer_id, uint32_t bytes, uint64_t *cursor) {
    const uint64_t page = 1ULL << REIST_NVIDIA_GK208_GPU_PAGE_SHIFT;
    const uint64_t table_end =
        REIST_NVIDIA_GK208_GR_SMALL_PAGE_TABLE_BASE +
        REIST_NVIDIA_GK208_GR_SMALL_PAGE_TABLE_BYTES;
    if (plan == NULL || cursor == NULL ||
        index >= REIST_NVIDIA_GK208_GR_GOLDEN_MAP_COUNT || bytes == 0U ||
        (*cursor & (page - 1ULL)) != 0ULL)
        return -84;
    const uint64_t mapped = ((uint64_t)bytes + page - 1ULL) & ~(page - 1ULL);
    if (mapped == 0ULL || *cursor <
            REIST_NVIDIA_GK208_GR_SMALL_PAGE_TABLE_BASE ||
        *cursor > table_end || mapped > table_end - *cursor)
        return -84;
    const uint64_t pte = (*cursor -
        REIST_NVIDIA_GK208_GR_SMALL_PAGE_TABLE_BASE) >>
        REIST_NVIDIA_GK208_GPU_PAGE_SHIFT;
    if (pte > UINT32_MAX || (mapped >>
            REIST_NVIDIA_GK208_GPU_PAGE_SHIFT) > UINT32_MAX)
        return -84;
    plan->maps[index] = (reist_nvidia_gk208_gr_golden_map_t){
        .gpu_address = *cursor,
        .buffer_id = buffer_id,
        .bytes = (uint32_t)mapped,
        .pte_first = (uint32_t)pte,
        .pte_count = (uint32_t)(mapped >>
            REIST_NVIDIA_GK208_GPU_PAGE_SHIFT),
        .flags = REIST_NVIDIA_GK208_GR_GOLDEN_MAP_VRAM |
            REIST_NVIDIA_GK208_GR_GOLDEN_MAP_READ |
            REIST_NVIDIA_GK208_GR_GOLDEN_MAP_WRITE,
    };
    *cursor += mapped;
    return 0;
}

static int gr_golden_plan_build(
        reist_nvidia_gk208_gr_golden_plan_t *plan,
        const reist_nvidia_gk208_gr_topology_t *topology,
        uint32_t context_size) {
    if (plan == NULL || topology == NULL ||
        reist_nvidia_gk208_gr_validate_topology(topology) != 0)
        return -22;
    reist_nvidia_gk208_gr_context_memory_plan_t memory;
    if (gr_context_memory_build(&memory, topology, context_size) != 0)
        return -84;
    for (uint32_t index = 0U; index < sizeof(*plan) / sizeof(uint32_t);
         ++index)
        ((uint32_t *)plan)[index] = 0U;
    plan->version = REIST_NVIDIA_GK208_GR_GOLDEN_PLAN_VERSION;
    plan->struct_size = sizeof(*plan);
    plan->topology_crc32 = gr_execution_topology_crc32(topology);
    plan->context_size = context_size;
    plan->context_tuple_count = REIST_GK208_GR_CONTEXT_TUPLE_COUNT;
    plan->context_crc32 = REIST_GK208_GR_CONTEXT_CRC32;
    plan->icmd_tuple_count = REIST_GK208_GR_ICMD_TUPLE_COUNT;
    plan->icmd_crc32 = REIST_GK208_GR_ICMD_CRC32;
    plan->mthd_tuple_count = REIST_GK208_GR_MTHD_TUPLE_COUNT;
    plan->mthd_crc32 = REIST_GK208_GR_MTHD_CRC32;
    plan->map_count = REIST_NVIDIA_GK208_GR_GOLDEN_MAP_COUNT;
    plan->phase_count = REIST_NVIDIA_GK208_GR_GOLDEN_PHASE_COUNT;
    plan->flags = REIST_NVIDIA_GK208_GR_GOLDEN_PLAN_HARDWARE_INACTIVE |
        REIST_NVIDIA_GK208_GR_GOLDEN_PLAN_OPAQUE_VRAM |
        REIST_NVIDIA_GK208_GR_GOLDEN_PLAN_COMPLETE;
    for (uint32_t phase = 0U;
         phase < REIST_NVIDIA_GK208_GR_GOLDEN_PHASE_COUNT; ++phase)
        plan->phases[phase] = phase + 1U;

    uint64_t cursor = REIST_NVIDIA_GK208_GR_GOLDEN_GPU_BASE;
    if (gr_golden_add_map(plan, 0U,
            REIST_NVIDIA_GK208_GR_GOLDEN_BUFFER_PAGEPOOL,
            memory.pagepool_bytes, &cursor) != 0 ||
        gr_golden_add_map(plan, 1U,
            REIST_NVIDIA_GK208_GR_GOLDEN_BUFFER_BUNDLE,
            memory.bundle_bytes, &cursor) != 0 ||
        gr_golden_add_map(plan, 2U,
            REIST_NVIDIA_GK208_GR_GOLDEN_BUFFER_ATTRIB,
            memory.attrib_bytes, &cursor) != 0 ||
        gr_golden_add_map(plan, 3U,
            REIST_NVIDIA_GK208_GR_GOLDEN_BUFFER_CONTEXT,
            memory.golden_bytes, &cursor) != 0)
        return -84;

    const uint32_t pagepool = (uint32_t)plan->maps[0].gpu_address;
    const uint32_t bundle = (uint32_t)plan->maps[1].gpu_address;
    const uint32_t attrib = (uint32_t)plan->maps[2].gpu_address;
    if (gr_golden_emit_patch(plan,
            REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_WRITE32,
            0x0040800CU, pagepool >> 8U) != 0 ||
        gr_golden_emit_patch(plan,
            REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_WRITE32,
            0x00408010U, 0x80000000U) != 0 ||
        gr_golden_emit_patch(plan,
            REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_WRITE32,
            0x00419004U, pagepool >> 8U) != 0 ||
        gr_golden_emit_patch(plan,
            REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_WRITE32,
            0x00419008U, 0U) != 0 ||
        gr_golden_emit_patch(plan,
            REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_WRITE32,
            0x004064CCU, 0x80000000U) != 0 ||
        gr_golden_emit_patch(plan,
            REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_WRITE32,
            0x00408004U, bundle >> 8U) != 0 ||
        gr_golden_emit_patch(plan,
            REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_WRITE32,
            0x00408008U, 0x80000000U |
                (REIST_NVIDIA_GK208_GR_BUNDLE_BYTES >> 8U)) != 0 ||
        gr_golden_emit_patch(plan,
            REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_WRITE32,
            0x00418808U, bundle >> 8U) != 0 ||
        gr_golden_emit_patch(plan,
            REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_WRITE32,
            0x0041880CU, 0x80000000U |
                (REIST_NVIDIA_GK208_GR_BUNDLE_BYTES >> 8U)) != 0 ||
        gr_golden_emit_patch(plan,
            REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_WRITE32,
            0x004064C8U, (0xC2U << 16U) | 0x200U) != 0 ||
        gr_golden_emit_patch(plan,
            REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_WRITE32,
            0x00418810U, 0x80000000U | (attrib >> 12U)) != 0 ||
        gr_golden_emit_patch(plan,
            REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_WRITE32,
            0x00419848U, 0x10000000U | (attrib >> 12U)) != 0 ||
        gr_golden_emit_patch(plan,
            REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_WRITE32,
            0x00405830U, (0x218U << 16U) | 0x648U) != 0 ||
        gr_golden_emit_patch(plan,
            REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_WRITE32,
            0x004064C4U, ((0x648U / 4U) << 16U) | 0xFFFFU) != 0)
        return -84;

    uint32_t beta_offset = 0U;
    uint32_t alpha_offset = REIST_NVIDIA_GK208_GR_ATTRIB_NR_MAX *
        topology->tpc_total;
    for (uint32_t gpc = 0U; gpc < topology->gpc_count; ++gpc) {
        const uint32_t tpcs = topology->tpc_count[gpc];
        const uint32_t ppc = 0x00503000U +
            gpc * REIST_NVIDIA_GK208_GPC_UNIT_STRIDE;
        if (gr_golden_emit_patch(plan,
                REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_WRITE32,
                ppc + 0xC0U,
                (1U << 28U) | ((0x218U * tpcs) << 16U) |
                    beta_offset) != 0)
            return -84;
        beta_offset += REIST_NVIDIA_GK208_GR_ATTRIB_NR_MAX * tpcs;
        if (gr_golden_emit_patch(plan,
                REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_WRITE32,
                ppc + 0xE4U,
                ((0x648U * tpcs) << 16U) | alpha_offset) != 0)
            return -84;
        alpha_offset += REIST_NVIDIA_GK208_GR_ALPHA_NR_MAX * tpcs;
    }
    if (gr_golden_emit_patch(plan,
            REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_COPY32,
            0x0017E91CU, 0x0017E91CU) != 0 ||
        gr_golden_emit_patch(plan,
            REIST_NVIDIA_GK208_GR_GOLDEN_PATCH_COPY32,
            0x0017E920U, 0x0017E920U) != 0)
        return -84;
    return 0;
}

int reist_nvidia_gk208_gr_compile_golden_plan(
        reist_nvidia_gk208_gr_golden_plan_t *plan,
        const reist_nvidia_gk208_gr_topology_t *topology,
        uint32_t context_size) {
    int status = gr_golden_plan_build(plan, topology, context_size);
    return status != 0 ? status :
        reist_nvidia_gk208_gr_validate_golden_plan(
            plan, topology, context_size);
}

int reist_nvidia_gk208_gr_validate_golden_plan(
        const reist_nvidia_gk208_gr_golden_plan_t *plan,
        const reist_nvidia_gk208_gr_topology_t *topology,
        uint32_t context_size) {
    if (plan == NULL || topology == NULL ||
        gr_table_crc32(reist_gk208_gr_context,
            REIST_GK208_GR_CONTEXT_TUPLE_COUNT) !=
                REIST_GK208_GR_CONTEXT_CRC32 ||
        gr_table_crc32(reist_gk208_gr_icmd,
            REIST_GK208_GR_ICMD_TUPLE_COUNT) !=
                REIST_GK208_GR_ICMD_CRC32 ||
        gr_method_table_crc32() != REIST_GK208_GR_MTHD_CRC32)
        return -84;
    for (uint32_t index = 0U; index < REIST_GK208_GR_ICMD_TUPLE_COUNT;
         ++index)
        if (!gr_icmd_tuple_valid(&reist_gk208_gr_icmd[index])) return -84;
    for (uint32_t index = 0U; index < REIST_GK208_GR_MTHD_TUPLE_COUNT;
         ++index)
        if (!gr_tuple_valid(&reist_gk208_gr_mthd[index].tuple) ||
            (reist_gk208_gr_mthd[index].class_id != 0x0000A197U &&
             reist_gk208_gr_mthd[index].class_id != 0x0000902DU))
            return -84;
    reist_nvidia_gk208_gr_golden_plan_t expected;
    if (gr_golden_plan_build(&expected, topology, context_size) != 0)
        return -84;
    const uint32_t *actual_words = (const uint32_t *)plan;
    const uint32_t *expected_words = (const uint32_t *)&expected;
    for (uint32_t index = 0U; index < sizeof(expected) / sizeof(uint32_t);
         ++index)
        if (actual_words[index] != expected_words[index]) return -84;
    return 0;
}

static int gr_execution_emit(gk208_gr_execution_builder_t *builder,
                             uint32_t opcode, uint32_t address,
                             uint32_t value, uint32_t mask) {
    if (builder == NULL || opcode < REIST_NVIDIA_GK208_GR_OP_WRITE32 ||
        opcode > REIST_NVIDIA_GK208_GR_OP_READ32_NONZERO ||
        builder->operation_count >=
            REIST_NVIDIA_GK208_GR_EXECUTION_OP_CAPACITY)
        return -84;
    const reist_nvidia_gk208_gr_execution_op_t operation = {
        .opcode = opcode,
        .address = address,
        .value = value,
        .mask = mask,
    };
    const uint32_t index = builder->operation_count;
    if (builder->output != NULL)
        builder->output->operations[index] = operation;
    if (builder->expected != NULL) {
        if (index >= builder->expected->header.operation_count)
            return -84;
        const reist_nvidia_gk208_gr_execution_op_t *actual =
            &builder->expected->operations[index];
        if (actual->opcode != operation.opcode ||
            actual->address != operation.address ||
            actual->value != operation.value ||
            actual->mask != operation.mask)
            return -84;
    }
    ++builder->operation_count;
    if (builder->section == GK208_GR_EXECUTION_SECTION_STATIC)
        ++builder->static_count;
    else if (builder->section == GK208_GR_EXECUTION_SECTION_ZBC)
        ++builder->zbc_count;
    else if (builder->section == GK208_GR_EXECUTION_SECTION_CONTEXT)
        ++builder->context_count;
    if (opcode == REIST_NVIDIA_GK208_GR_OP_VRAM_OFFSET32)
        ++builder->vram_count;
    return 0;
}

static int gr_execution_write(gk208_gr_execution_builder_t *builder,
                              uint32_t address, uint32_t value) {
    return gr_execution_emit(builder, REIST_NVIDIA_GK208_GR_OP_WRITE32,
                             address, value, 0U);
}

static int gr_execution_mask(gk208_gr_execution_builder_t *builder,
                             uint32_t address, uint32_t mask,
                             uint32_t value) {
    return gr_execution_emit(builder, REIST_NVIDIA_GK208_GR_OP_MASK32,
                             address, value, mask);
}

static uint32_t gr_gpc_unit(uint32_t gpc, uint32_t offset) {
    return REIST_NVIDIA_GK208_GPC_UNIT_BASE +
        gpc * REIST_NVIDIA_GK208_GPC_UNIT_STRIDE + offset;
}

static uint32_t gr_tpc_unit(uint32_t gpc, uint32_t tpc,
                            uint32_t offset) {
    return GK208_GR_TPC_UNIT_BASE +
        gpc * REIST_NVIDIA_GK208_GPC_UNIT_STRIDE +
        tpc * GK208_GR_TPC_UNIT_STRIDE + offset;
}

static uint32_t gr_ppc_unit(uint32_t gpc, uint32_t ppc,
                            uint32_t offset) {
    return GK208_GR_PPC_UNIT_BASE +
        gpc * REIST_NVIDIA_GK208_GPC_UNIT_STRIDE +
        ppc * GK208_GR_PPC_UNIT_STRIDE + offset;
}

static uint32_t gr_rop_unit(uint32_t rop, uint32_t offset) {
    return GK208_GR_ROP_UNIT_BASE + rop * GK208_GR_ROP_UNIT_STRIDE + offset;
}

static int gr_execution_ltc_color(
    gk208_gr_execution_builder_t *builder, uint32_t slot,
    const uint32_t color[4]) {
    if (gr_execution_mask(builder, GK208_GR_LTC_ZBC_INDEX, 0xFU,
                          slot) != 0)
        return -84;
    for (uint32_t word = 0U; word < 4U; ++word)
        if (gr_execution_write(builder,
                GK208_GR_LTC_ZBC_COLOR + word * sizeof(uint32_t),
                color[word]) != 0)
            return -84;
    return 0;
}

static int gr_execution_ltc_depth(
    gk208_gr_execution_builder_t *builder, uint32_t slot,
    uint32_t depth) {
    if (gr_execution_mask(builder, GK208_GR_LTC_ZBC_INDEX, 0xFU,
                          slot) != 0 ||
        gr_execution_write(builder, GK208_GR_LTC_ZBC_DEPTH, depth) != 0)
        return -84;
    return 0;
}

static int gr_execution_pgraph_color(
    gk208_gr_execution_builder_t *builder, uint32_t slot,
    uint32_t format, const uint32_t ds[4]) {
    if (format != 0U) {
        for (uint32_t word = 0U; word < 4U; ++word)
            if (gr_execution_write(builder,
                    0x00405804U + word * sizeof(uint32_t), ds[word]) != 0)
                return -84;
    }
    if (gr_execution_write(builder, 0x00405814U, format) != 0 ||
        gr_execution_write(builder, 0x00405820U, slot) != 0 ||
        gr_execution_write(builder, 0x00405824U, 4U) != 0)
        return -84;
    return 0;
}

static int gr_execution_pgraph_depth(
    gk208_gr_execution_builder_t *builder, uint32_t slot,
    uint32_t format, uint32_t ds) {
    if (format != 0U &&
        gr_execution_write(builder, 0x00405818U, ds) != 0)
        return -84;
    if (gr_execution_write(builder, 0x0040581CU, format) != 0 ||
        gr_execution_write(builder, 0x00405820U, slot) != 0 ||
        gr_execution_write(builder, 0x00405824U, 5U) != 0)
        return -84;
    return 0;
}

static int gr_execution_ltc_zbc_reset(
    gk208_gr_execution_builder_t *builder) {
    const uint32_t zero[4] = {0U, 0U, 0U, 0U};
    builder->section = GK208_GR_EXECUTION_SECTION_ZBC;
    for (uint32_t slot = GK208_GR_ZBC_SLOT_MIN;
         slot <= GK208_GR_ZBC_SLOT_MAX; ++slot)
        if (gr_execution_ltc_color(builder, slot, zero) != 0)
            return -84;
    for (uint32_t slot = GK208_GR_ZBC_SLOT_MIN;
         slot <= GK208_GR_ZBC_SLOT_MAX; ++slot)
        if (gr_execution_ltc_depth(builder, slot, 0U) != 0)
            return -84;
    builder->section = GK208_GR_EXECUTION_SECTION_GENERAL;
    return 0;
}

static uint32_t gr_screen_tile_row_offset(uint32_t tpc_total) {
    static const uint32_t primes[] = {
        3U, 5U, 7U, 11U, 13U, 17U, 19U, 23U, 29U,
        31U, 37U, 41U, 43U, 47U, 53U, 59U, 61U,
    };
    switch (tpc_total) {
    case 15U: return 6U;
    case 14U: return 5U;
    case 13U: return 2U;
    case 11U: return 7U;
    case 10U: return 6U;
    case 7U:
    case 5U: return 1U;
    case 3U: return 2U;
    case 2U:
    case 1U: return 1U;
    default:
        for (uint32_t index = 0U;
             index < sizeof(primes) / sizeof(primes[0]); ++index)
            if (tpc_total % primes[index] != 0U) return primes[index];
        return 3U;
    }
}

static int gr_build_tile_map(
    const reist_nvidia_gk208_gr_topology_t *topology,
    uint32_t tile[REIST_NVIDIA_GK208_MAX_TOTAL_TPCS]) {
    int32_t initial_fraction[REIST_NVIDIA_GK208_MAX_GPCS];
    int32_t initial_error[REIST_NVIDIA_GK208_MAX_GPCS];
    int32_t running_error[REIST_NVIDIA_GK208_MAX_GPCS];
    uint32_t gpc_map[REIST_NVIDIA_GK208_MAX_GPCS];
    for (uint32_t gpc = 0U; gpc < topology->gpc_count; ++gpc)
        gpc_map[gpc] = gpc;
    for (uint32_t pass = 0U; pass < topology->gpc_count; ++pass) {
        uint32_t changed = 0U;
        for (uint32_t gpc = 0U; gpc + 1U < topology->gpc_count; ++gpc) {
            if (topology->tpc_count[gpc_map[gpc + 1U]] >
                topology->tpc_count[gpc_map[gpc]]) {
                const uint32_t swap = gpc_map[gpc];
                gpc_map[gpc] = gpc_map[gpc + 1U];
                gpc_map[gpc + 1U] = swap;
                changed = 1U;
            }
        }
        if (changed == 0U) break;
    }
    uint32_t multiplier = topology->gpc_count * topology->tpc_max;
    multiplier = (multiplier & 1U) != 0U ? 2U : 1U;
    const int32_t denominator = (int32_t)(
        topology->gpc_count * topology->tpc_max * multiplier);
    for (uint32_t gpc = 0U; gpc < topology->gpc_count; ++gpc) {
        initial_fraction[gpc] = (int32_t)(
            topology->tpc_count[gpc_map[gpc]] *
            topology->gpc_count * multiplier);
        initial_error[gpc] = (int32_t)(
            gpc * topology->tpc_max * multiplier) - denominator / 2;
        running_error[gpc] = initial_fraction[gpc] + initial_error[gpc];
    }
    uint32_t tile_count = 0U;
    const uint32_t cycle_limit = REIST_NVIDIA_GK208_MAX_TOTAL_TPCS *
        REIST_NVIDIA_GK208_MAX_GPCS * 2U;
    for (uint32_t cycle = 0U;
         cycle < cycle_limit && tile_count < topology->tpc_total; ++cycle) {
        for (uint32_t gpc = 0U;
             gpc < topology->gpc_count && tile_count < topology->tpc_total;
             ++gpc) {
            if (running_error[gpc] * 2 >= denominator) {
                tile[tile_count++] = gpc_map[gpc];
                running_error[gpc] += initial_fraction[gpc] - denominator;
            } else {
                running_error[gpc] += initial_fraction[gpc];
            }
        }
    }
    return tile_count == topology->tpc_total ? 0 : -84;
}

static int gr_execution_zcull(
    gk208_gr_execution_builder_t *builder,
    const reist_nvidia_gk208_gr_topology_t *topology) {
    uint32_t tile[REIST_NVIDIA_GK208_MAX_TOTAL_TPCS];
    uint32_t bank[REIST_NVIDIA_GK208_MAX_GPCS];
    for (uint32_t index = 0U; index < REIST_NVIDIA_GK208_MAX_TOTAL_TPCS;
         ++index)
        tile[index] = 0U;
    for (uint32_t index = 0U; index < REIST_NVIDIA_GK208_MAX_GPCS;
         ++index)
        bank[index] = 0U;
    if (gr_build_tile_map(topology, tile) != 0) return -84;
    for (uint32_t first = 0U; first < GK208_GR_ZCULL_TILE_SLOTS;
         first += 8U) {
        uint32_t value = 0U;
        for (uint32_t item = 0U;
             item < 8U && first + item < topology->tpc_total; ++item) {
            const uint32_t gpc = tile[first + item];
            if (gpc >= topology->gpc_count || bank[gpc] >= 16U)
                return -84;
            value |= bank[gpc] << (item * 4U);
            ++bank[gpc];
        }
        if (gr_execution_write(builder,
                GK208_GR_GPC_BCAST_BASE + 0x0980U + (first / 8U) * 4U,
                value) != 0)
            return -84;
    }
    const uint32_t magic =
        (0x00800000U + topology->tpc_total - 1U) / topology->tpc_total;
    const uint32_t row = gr_screen_tile_row_offset(topology->tpc_total);
    for (uint32_t gpc = 0U; gpc < topology->gpc_count; ++gpc) {
        if (gr_execution_write(builder, gr_gpc_unit(gpc, 0x0914U),
                (row << 8U) | topology->tpc_count[gpc]) != 0 ||
            gr_execution_write(builder, gr_gpc_unit(gpc, 0x0910U),
                0x00040000U | topology->tpc_total) != 0 ||
            gr_execution_write(builder, gr_gpc_unit(gpc, 0x0918U),
                magic) != 0)
            return -84;
    }
    return gr_execution_write(
        builder, GK208_GR_GPC_BCAST_BASE + 0x3FD4U, magic);
}

static int gr_execution_static_mmio(
    gk208_gr_execution_builder_t *builder) {
    builder->section = GK208_GR_EXECUTION_SECTION_STATIC;
    for (uint32_t index = 0U; index < REIST_GK208_GR_MMIO_TUPLE_COUNT;
         ++index) {
        const reist_nvidia_gk208_gr_tuple_t *tuple =
            &reist_gk208_gr_mmio[index];
        if (!gr_tuple_valid(tuple)) return -84;
        uint32_t address = tuple->address;
        for (uint32_t item = 0U; item < tuple->count; ++item) {
            if (gr_execution_write(builder, address, tuple->value) != 0)
                return -84;
            address += tuple->pitch;
        }
    }
    builder->section = GK208_GR_EXECUTION_SECTION_GENERAL;
    return 0;
}

static int gr_execution_zbc_defaults(
    gk208_gr_execution_builder_t *builder) {
    static const uint32_t color_format[4] = {1U, 2U, 4U, 4U};
    static const uint32_t color_ds[4][4] = {
        {0U, 0U, 0U, 0U},
        {0x3F800000U, 0x3F800000U, 0x3F800000U, 0x3F800000U},
        {0U, 0U, 0U, 0U},
        {0x3F800000U, 0x3F800000U, 0x3F800000U, 0x3F800000U},
    };
    static const uint32_t color_ltc[4][4] = {
        {0U, 0U, 0U, 0U},
        {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX},
        {0U, 0U, 0U, 0U},
        {0x3F800000U, 0x3F800000U, 0x3F800000U, 0x3F800000U},
    };
    static const uint32_t zero[4] = {0U, 0U, 0U, 0U};
    builder->section = GK208_GR_EXECUTION_SECTION_ZBC;
    for (uint32_t index = 0U; index < 4U; ++index) {
        const uint32_t slot = index + GK208_GR_ZBC_SLOT_MIN;
        if (gr_execution_ltc_color(builder, slot, color_ltc[index]) != 0 ||
            gr_execution_pgraph_color(builder, slot, color_format[index],
                                      color_ds[index]) != 0)
            return -84;
    }
    for (uint32_t slot = 5U; slot <= GK208_GR_ZBC_SLOT_MAX; ++slot)
        if (gr_execution_pgraph_color(builder, slot, 0U, zero) != 0)
            return -84;
    if (gr_execution_ltc_depth(builder, 1U, 0U) != 0 ||
        gr_execution_pgraph_depth(builder, 1U, 1U, 0U) != 0 ||
        gr_execution_ltc_depth(builder, 2U, 0x3F800000U) != 0 ||
        gr_execution_pgraph_depth(builder, 2U, 1U, 0x3F800000U) != 0)
        return -84;
    for (uint32_t slot = 3U; slot <= GK208_GR_ZBC_SLOT_MAX; ++slot)
        if (gr_execution_pgraph_depth(builder, slot, 0U, 0U) != 0)
            return -84;
    builder->section = GK208_GR_EXECUTION_SECTION_GENERAL;
    return 0;
}

static int gr_execution_context(
    gk208_gr_execution_builder_t *builder) {
    reist_nvidia_gk208_gr_context_plan_t plan;
    if (reist_nvidia_gk208_gr_compile_context_plan(&plan) != 0 ||
        reist_nvidia_gk208_gr_validate_context_plan(&plan) != 0)
        return -84;
    builder->section = GK208_GR_EXECUTION_SECTION_CONTEXT;
    for (uint32_t group = 0U;
         group < REIST_NVIDIA_GK208_GR_CONTEXT_PACK_COUNT; ++group) {
        const reist_nvidia_gk208_gr_context_span_t *span =
            &reist_gk208_gr_context_spans[group];
        if (gr_execution_emit(builder,
                REIST_NVIDIA_GK208_GR_OP_CONTEXT_GROUP,
                span->falcon_base, span->starstar,
                plan.group_count[group]) != 0)
            return -84;
        for (uint32_t item = 0U; item < plan.group_count[group]; ++item) {
            const uint32_t word = plan.words[plan.group_first[group] + item];
            if (gr_execution_emit(builder,
                    REIST_NVIDIA_GK208_GR_OP_CONTEXT_TRANSFER,
                    span->falcon_base + 0x01C4U, word, 0U) != 0)
                return -84;
        }
    }
    if (gr_execution_write(builder, 0x0040910CU, 0U) != 0 ||
        gr_execution_write(builder, 0x00409100U, 2U) != 0 ||
        gr_execution_emit(builder, REIST_NVIDIA_GK208_GR_OP_WAIT_MASK32,
            0x00409800U, 0x80000000U, 2000U) != 0 ||
        gr_execution_emit(builder, REIST_NVIDIA_GK208_GR_OP_READ32_NONZERO,
            0x00409804U, 0U, 0U) != 0)
        return -84;
    builder->section = GK208_GR_EXECUTION_SECTION_GENERAL;
    return 0;
}

static int gr_execution_compile_internal(
    gk208_gr_execution_builder_t *builder,
    const reist_nvidia_gk208_gr_topology_t *topology) {
    if (builder == NULL ||
        reist_nvidia_gk208_gr_validate_topology(topology) != 0)
        return -84;

    /* nvkm_ltc_init() establishes zero ZBC state before gf100_gr_init(). */
    if (gr_execution_ltc_zbc_reset(builder) != 0 ||
        gr_execution_mask(builder, 0x00400500U, 0x00010001U, 0U) != 0 ||
        gr_execution_emit(builder,
            REIST_NVIDIA_GK208_GR_OP_COPY_MASKED32,
            0x00418880U, 0x00100C80U, 0x00000001U) != 0 ||
        gr_execution_write(builder, 0x004188A4U, 0x03000000U) != 0 ||
        gr_execution_write(builder, 0x00418888U, 0U) != 0 ||
        gr_execution_write(builder, 0x0041888CU, 0U) != 0 ||
        gr_execution_write(builder, 0x00418890U, 0U) != 0 ||
        gr_execution_write(builder, 0x00418894U, 0U) != 0 ||
        gr_execution_emit(builder,
            REIST_NVIDIA_GK208_GR_OP_VRAM_OFFSET32,
            0x004188B4U, REIST_NVIDIA_GK208_GR_VRAM_BUFFER_MMU_WRITE,
            REIST_NVIDIA_GK208_GR_VRAM_ADDRESS_SHIFT) != 0 ||
        gr_execution_emit(builder,
            REIST_NVIDIA_GK208_GR_OP_VRAM_OFFSET32,
            0x004188B8U, REIST_NVIDIA_GK208_GR_VRAM_BUFFER_MMU_READ,
            REIST_NVIDIA_GK208_GR_VRAM_ADDRESS_SHIFT) != 0 ||
        gr_execution_static_mmio(builder) != 0 ||
        gr_execution_emit(builder, REIST_NVIDIA_GK208_GR_OP_WAIT_IDLE,
            0x00400700U, 0x0040060CU,
            REIST_NVIDIA_GK208_GR_IDLE_DEADLINE_MS) != 0 ||
        gr_execution_write(builder, gr_gpc_unit(0U, 0x3018U), 1U) != 0 ||
        gr_execution_zcull(builder, topology) != 0 ||
        gr_execution_emit(builder,
            REIST_NVIDIA_GK208_GR_OP_COPY_MASKED32,
            GK208_GR_GPC_BCAST_BASE + 0x08ACU,
            0x00100800U, UINT32_MAX) != 0 ||
        gr_execution_emit(builder,
            REIST_NVIDIA_GK208_GR_OP_COPY_MASKED32,
            0x00408850U, 0x00120074U, 0x0000000FU) != 0 ||
        gr_execution_emit(builder,
            REIST_NVIDIA_GK208_GR_OP_COPY_MASKED32,
            0x00408958U, 0x00120074U, 0x0000000FU) != 0 ||
        gr_execution_write(builder, 0x00400500U, 0x00010001U) != 0 ||
        gr_execution_write(builder, 0x00400100U, UINT32_MAX) != 0 ||
        gr_execution_write(builder, 0x0040013CU, UINT32_MAX) != 0 ||
        gr_execution_write(builder, 0x00400124U, 2U) != 0 ||
        gr_execution_write(builder, 0x00409C24U, 0x000E0001U) != 0 ||
        gr_execution_write(builder, 0x00404000U, 0xC0000000U) != 0 ||
        gr_execution_write(builder, 0x00404600U, 0xC0000000U) != 0 ||
        gr_execution_write(builder, 0x00408030U, 0xC0000000U) != 0 ||
        gr_execution_write(builder, 0x00406018U, 0xC0000000U) != 0 ||
        gr_execution_write(builder, 0x00404490U, 0xC0000000U) != 0 ||
        gr_execution_write(builder, 0x00407020U, 0x40000000U) != 0 ||
        gr_execution_write(builder, 0x00405840U, 0xC0000000U) != 0 ||
        gr_execution_write(builder, 0x00405844U, 0x00FFFFFFU) != 0 ||
        gr_execution_mask(builder, 0x00419CC0U, 0x00000008U,
                          0x00000008U) != 0)
        return -84;

    for (uint32_t gpc = 0U; gpc < topology->gpc_count; ++gpc)
        for (uint32_t tpc = 0U; tpc < topology->tpc_count[gpc]; ++tpc)
            if (gr_execution_write(builder,
                    gr_tpc_unit(gpc, tpc, 0x048CU), 0xC0000000U) != 0)
                return -84;

    for (uint32_t gpc = 0U; gpc < topology->gpc_count; ++gpc)
        if (gr_execution_write(builder,
                gr_ppc_unit(gpc, 0U, 0x0038U), 0xC0000000U) != 0)
            return -84;

    for (uint32_t gpc = 0U; gpc < topology->gpc_count; ++gpc) {
        if (gr_execution_write(builder, gr_gpc_unit(gpc, 0x0420U),
                0xC0000000U) != 0 ||
            gr_execution_write(builder, gr_gpc_unit(gpc, 0x0900U),
                0xC0000000U) != 0 ||
            gr_execution_write(builder, gr_gpc_unit(gpc, 0x1028U),
                0xC0000000U) != 0 ||
            gr_execution_write(builder, gr_gpc_unit(gpc, 0x0824U),
                0xC0000000U) != 0)
            return -84;
        for (uint32_t tpc = 0U; tpc < topology->tpc_count[gpc]; ++tpc) {
            if (gr_execution_write(builder, gr_tpc_unit(gpc, tpc, 0x0508U),
                    UINT32_MAX) != 0 ||
                gr_execution_write(builder, gr_tpc_unit(gpc, tpc, 0x050CU),
                    UINT32_MAX) != 0 ||
                gr_execution_write(builder, gr_tpc_unit(gpc, tpc, 0x0224U),
                    0xC0000000U) != 0 ||
                gr_execution_write(builder, gr_tpc_unit(gpc, tpc, 0x0084U),
                    0xC0000000U) != 0 ||
                gr_execution_write(builder, gr_tpc_unit(gpc, tpc, 0x0644U),
                    0x001FFFFEU) != 0 ||
                gr_execution_write(builder, gr_tpc_unit(gpc, tpc, 0x064CU),
                    0x0000000FU) != 0)
                return -84;
        }
        if (gr_execution_write(builder, gr_gpc_unit(gpc, 0x2C90U),
                UINT32_MAX) != 0 ||
            gr_execution_write(builder, gr_gpc_unit(gpc, 0x2C94U),
                UINT32_MAX) != 0)
            return -84;
    }

    for (uint32_t rop = 0U; rop < topology->rop_count; ++rop)
        if (gr_execution_write(builder, gr_rop_unit(rop, 0x0144U),
                0x40000000U) != 0 ||
            gr_execution_write(builder, gr_rop_unit(rop, 0x0070U),
                0x40000000U) != 0 ||
            gr_execution_write(builder, gr_rop_unit(rop, 0x0204U),
                UINT32_MAX) != 0 ||
            gr_execution_write(builder, gr_rop_unit(rop, 0x0208U),
                UINT32_MAX) != 0)
            return -84;

    if (gr_execution_write(builder, 0x00400108U, UINT32_MAX) != 0 ||
        gr_execution_write(builder, 0x00400138U, UINT32_MAX) != 0 ||
        gr_execution_write(builder, 0x00400118U, UINT32_MAX) != 0 ||
        gr_execution_write(builder, 0x00400130U, UINT32_MAX) != 0 ||
        gr_execution_write(builder, 0x0040011CU, UINT32_MAX) != 0 ||
        gr_execution_write(builder, 0x00400134U, UINT32_MAX) != 0 ||
        gr_execution_write(builder, 0x00400054U, 0x34CE3464U) != 0 ||
        gr_execution_zbc_defaults(builder) != 0 ||
        gr_execution_context(builder) != 0)
        return -84;
    return 0;
}

uint32_t reist_nvidia_gk208_gr_execution_used_bytes(
    const reist_nvidia_gk208_gr_execution_image_t *image) {
    if (image == NULL ||
        image->header.version != REIST_NVIDIA_GK208_GR_EXECUTION_VERSION ||
        image->header.header_size !=
            REIST_NVIDIA_GK208_GR_EXECUTION_HEADER_BYTES ||
        image->header.operation_count == 0U ||
        image->header.operation_count >
            REIST_NVIDIA_GK208_GR_EXECUTION_OP_CAPACITY)
        return 0U;
    const uint32_t used = REIST_NVIDIA_GK208_GR_EXECUTION_HEADER_BYTES +
        image->header.operation_count *
            REIST_NVIDIA_GK208_GR_EXECUTION_OP_BYTES;
    return image->header.used_bytes == used ? used : 0U;
}

int reist_nvidia_gk208_gr_compile_execution_image(
    reist_nvidia_gk208_gr_execution_image_t *image,
    const reist_nvidia_gk208_gr_topology_t *topology) {
    if (image == NULL || topology == NULL) return -22;
    if (reist_nvidia_gk208_gr_validate_topology(topology) != 0)
        return -84;
    image->header = (reist_nvidia_gk208_gr_execution_header_t){0};
    for (uint32_t index = 0U;
         index < REIST_NVIDIA_GK208_GR_EXECUTION_OP_CAPACITY; ++index)
        image->operations[index] =
            (reist_nvidia_gk208_gr_execution_op_t){0};
    gk208_gr_execution_builder_t builder = {
        .output = image,
        .expected = NULL,
        .section = GK208_GR_EXECUTION_SECTION_GENERAL,
    };
    if (gr_execution_compile_internal(&builder, topology) != 0 ||
        builder.operation_count == 0U ||
        builder.vram_count != REIST_NVIDIA_GK208_GR_VRAM_RELOCATION_COUNT)
        return -84;
    const uint32_t used = REIST_NVIDIA_GK208_GR_EXECUTION_HEADER_BYTES +
        builder.operation_count * REIST_NVIDIA_GK208_GR_EXECUTION_OP_BYTES;
    if (used > REIST_NVIDIA_GK208_GR_EXECUTION_MAX_BYTES) return -84;
    image->header = (reist_nvidia_gk208_gr_execution_header_t){
        .version = REIST_NVIDIA_GK208_GR_EXECUTION_VERSION,
        .header_size = REIST_NVIDIA_GK208_GR_EXECUTION_HEADER_BYTES,
        .used_bytes = used,
        .operation_count = builder.operation_count,
        .operation_crc32 = gr_execution_operation_crc32(
            image->operations, builder.operation_count),
        .topology_crc32 = gr_execution_topology_crc32(topology),
        .static_mmio_operation_count = builder.static_count,
        .zbc_operation_count = builder.zbc_count,
        .context_operation_count = builder.context_count,
        .vram_relocation_count = builder.vram_count,
        .flags = REIST_NVIDIA_GK208_GR_EXECUTION_FLAG_HARDWARE_INACTIVE |
            REIST_NVIDIA_GK208_GR_EXECUTION_FLAG_NOFW |
            REIST_NVIDIA_GK208_GR_EXECUTION_FLAG_UNRESOLVED_VRAM,
        .gpc_count = topology->gpc_count,
        .tpc_total = topology->tpc_total,
        .rop_count = topology->rop_count,
    };
    return 0;
}

int reist_nvidia_gk208_gr_validate_execution_image(
    const reist_nvidia_gk208_gr_execution_image_t *image,
    const reist_nvidia_gk208_gr_topology_t *topology) {
    if (image == NULL || topology == NULL) return -22;
    if (reist_nvidia_gk208_gr_validate_topology(topology) != 0 ||
        reist_nvidia_gk208_gr_execution_used_bytes(image) == 0U ||
        image->header.reserved[0] != 0U ||
        image->header.reserved[1] != 0U ||
        image->header.flags !=
            (REIST_NVIDIA_GK208_GR_EXECUTION_FLAG_HARDWARE_INACTIVE |
             REIST_NVIDIA_GK208_GR_EXECUTION_FLAG_NOFW |
             REIST_NVIDIA_GK208_GR_EXECUTION_FLAG_UNRESOLVED_VRAM) ||
        image->header.gpc_count != topology->gpc_count ||
        image->header.tpc_total != topology->tpc_total ||
        image->header.rop_count != topology->rop_count ||
        image->header.topology_crc32 !=
            gr_execution_topology_crc32(topology) ||
        image->header.operation_crc32 != gr_execution_operation_crc32(
            image->operations, image->header.operation_count))
        return -84;
    gk208_gr_execution_builder_t builder = {
        .output = NULL,
        .expected = image,
        .section = GK208_GR_EXECUTION_SECTION_GENERAL,
    };
    if (gr_execution_compile_internal(&builder, topology) != 0 ||
        builder.operation_count != image->header.operation_count ||
        builder.static_count != image->header.static_mmio_operation_count ||
        builder.zbc_count != image->header.zbc_operation_count ||
        builder.context_count != image->header.context_operation_count ||
        builder.vram_count != image->header.vram_relocation_count ||
        builder.vram_count != REIST_NVIDIA_GK208_GR_VRAM_RELOCATION_COUNT)
        return -84;
    return 0;
}

int reist_nvidia_gk208_gr_execution_self_test(void) {
    static reist_nvidia_gk208_gr_execution_image_t image;
    reist_nvidia_gk208_gr_topology_t topology;
    topology.version = REIST_NVIDIA_GK208_GR_PLAN_VERSION;
    topology.struct_size = sizeof(topology);
    topology.gpc_count = 1U;
    topology.rop_count = 2U;
    topology.tpc_total = 2U;
    topology.tpc_max = 2U;
    for (uint32_t index = 0U; index < REIST_NVIDIA_GK208_MAX_GPCS;
         ++index) {
        topology.tpc_count[index] = 0U;
        topology.ppc_tpc_mask[index] = 0U;
    }
    topology.tpc_count[0] = 2U;
    topology.ppc_tpc_mask[0] = 3U;
    topology.reserved[0] = 0U;
    topology.reserved[1] = 0U;
    if (reist_nvidia_gk208_gr_compile_execution_image(&image, &topology) != 0 ||
        reist_nvidia_gk208_gr_validate_execution_image(&image, &topology) != 0 ||
        image.header.vram_relocation_count != 2U ||
        image.header.static_mmio_operation_count == 0U ||
        image.header.zbc_operation_count == 0U ||
        image.header.context_operation_count == 0U ||
        image.operations[0].opcode != REIST_NVIDIA_GK208_GR_OP_MASK32 ||
        image.operations[0].address != GK208_GR_LTC_ZBC_INDEX)
        return -84;
    const uint32_t saved = image.operations[0].value;
    image.operations[0].value ^= 1U;
    if (reist_nvidia_gk208_gr_validate_execution_image(
            &image, &topology) != -84)
        return -84;
    image.operations[0].value = saved;
    if (reist_nvidia_gk208_gr_validate_execution_image(
            &image, &topology) != 0)
        return -84;
    ++topology.rop_count;
    if (reist_nvidia_gk208_gr_validate_execution_image(
            &image, &topology) != -84)
        return -84;
    return 0;
}

int reist_nvidia_gk208_gr_context_memory_self_test(void) {
    reist_nvidia_gk208_gr_topology_t topology;
    topology.version = REIST_NVIDIA_GK208_GR_PLAN_VERSION;
    topology.struct_size = sizeof(topology);
    topology.gpc_count = 1U;
    topology.rop_count = 2U;
    topology.tpc_total = 2U;
    topology.tpc_max = 2U;
    for (uint32_t index = 0U; index < REIST_NVIDIA_GK208_MAX_GPCS;
         ++index) {
        topology.tpc_count[index] = 0U;
        topology.ppc_tpc_mask[index] = 0U;
    }
    topology.tpc_count[0] = 2U;
    topology.ppc_tpc_mask[0] = 3U;
    topology.reserved[0] = 0U;
    topology.reserved[1] = 0U;
    reist_nvidia_gk208_gr_context_memory_plan_t plan;
    if (reist_nvidia_gk208_gr_compile_context_memory_plan(
            &plan, &topology, 0x2000U) != 0 ||
        plan.topology_crc32 != gr_execution_topology_crc32(&topology) ||
        plan.attrib_bytes != 0x0002C8C0U ||
        plan.golden_bytes != 0x00082000U ||
        plan.total_bytes != 0x000BA000U)
        return -84;
    plan.attrib_bytes += 0x1000U;
    return reist_nvidia_gk208_gr_validate_context_memory_plan(
        &plan, &topology, 0x2000U) == -84 ? 0 : -84;
}

int reist_nvidia_gk208_gr_golden_plan_self_test(void) {
    reist_nvidia_gk208_gr_topology_t topology;
    topology.version = REIST_NVIDIA_GK208_GR_PLAN_VERSION;
    topology.struct_size = sizeof(topology);
    topology.gpc_count = 1U;
    topology.rop_count = 2U;
    topology.tpc_total = 2U;
    topology.tpc_max = 2U;
    for (uint32_t index = 0U; index < REIST_NVIDIA_GK208_MAX_GPCS;
         ++index) {
        topology.tpc_count[index] = 0U;
        topology.ppc_tpc_mask[index] = 0U;
    }
    topology.tpc_count[0] = 2U;
    topology.ppc_tpc_mask[0] = 3U;
    topology.reserved[0] = 0U;
    topology.reserved[1] = 0U;
    reist_nvidia_gk208_gr_golden_plan_t plan;
    if (reist_nvidia_gk208_gr_compile_golden_plan(
            &plan, &topology, 0x2000U) != 0 ||
        plan.map_count != REIST_NVIDIA_GK208_GR_GOLDEN_MAP_COUNT ||
        plan.patch_count != 18U ||
        plan.phase_count != REIST_NVIDIA_GK208_GR_GOLDEN_PHASE_COUNT ||
        plan.maps[0].gpu_address !=
            REIST_NVIDIA_GK208_GR_GOLDEN_GPU_BASE ||
        plan.maps[0].pte_first != 16U ||
        plan.maps[2].bytes != 0x0002D000U ||
        plan.maps[3].gpu_address != 0x0000000020048000ULL ||
        plan.phases[REIST_NVIDIA_GK208_GR_GOLDEN_PHASE_COUNT - 1U] !=
            REIST_NVIDIA_GK208_GR_GOLDEN_PHASE_RETAIN)
        return -84;
    plan.patches[0].value ^= 1U;
    return reist_nvidia_gk208_gr_validate_golden_plan(
        &plan, &topology, 0x2000U) == -84 ? 0 : -84;
}
