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
