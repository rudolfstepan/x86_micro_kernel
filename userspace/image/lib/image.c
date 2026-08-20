/** @file image.c @brief Bounded BMP and GIF raster decoders. */
#include "reist/image.h"

#include <limits.h>

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
        ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static int dimensions(uint32_t width, uint32_t height, size_t capacity) {
    return width != 0U && height != 0U && width <= REIST_IMAGE_MAX_WIDTH &&
        height <= REIST_IMAGE_MAX_HEIGHT &&
        (size_t)width <= SIZE_MAX / height &&
        (size_t)width * height <= capacity;
}

static void publish(reist_image_info_t *info, uint32_t width, uint32_t height,
                    uint32_t format, uint32_t frames, uint32_t flags) {
    *info = (reist_image_info_t){REIST_IMAGE_API_VERSION, sizeof(*info),
        width, height, width, format, frames, flags};
}

static int decode_bmp(const uint8_t *b, size_t n, uint32_t *pixels,
                      size_t capacity, reist_image_info_t *info) {
    if (n < 54U || b[0] != 'B' || b[1] != 'M') return -84;
    uint32_t offset = le32(&b[10]), dib = le32(&b[14]);
    int32_t signed_width = (int32_t)le32(&b[18]);
    int32_t signed_height = (int32_t)le32(&b[22]);
    uint16_t planes = le16(&b[26]), bits = le16(&b[28]);
    if (dib < 40U || (size_t)14U + dib > n || signed_width <= 0 ||
        signed_height == 0 || signed_height == INT32_MIN || planes != 1U ||
        (bits != 24U && bits != 32U) || le32(&b[30]) != 0U) return -95;
    uint32_t width = (uint32_t)signed_width;
    uint32_t height = signed_height < 0 ? (uint32_t)-signed_height
                                        : (uint32_t)signed_height;
    if (!dimensions(width, height, capacity)) return -75;
    uint64_t row = (((uint64_t)width * bits + 31U) / 32U) * 4U;
    uint64_t bytes = row * height;
    if (offset < 14U + dib || offset > n || bytes > n - offset) return -84;
    uint32_t cpp = bits / 8U;
    for (uint32_t y = 0U; y < height; ++y) {
        uint32_t source_y = signed_height < 0 ? y : height - 1U - y;
        const uint8_t *source = &b[offset + (size_t)source_y * (size_t)row];
        for (uint32_t x = 0U; x < width; ++x) {
            const uint8_t *p = &source[x * cpp];
            pixels[(size_t)y * width + x] =
                ((uint32_t)p[2] << 16U) | ((uint32_t)p[1] << 8U) | p[0];
        }
    }
    publish(info, width, height, REIST_IMAGE_FORMAT_BMP, 1U, 0U);
    return 0;
}

typedef struct gif_bits {
    const uint8_t *data;
    size_t size;
    size_t byte;
    uint8_t bit;
} gif_bits_t;

static int gif_code(gif_bits_t *bits, uint32_t width, uint16_t *code) {
    uint32_t value = 0U;
    for (uint32_t i = 0U; i < width; ++i) {
        if (bits->byte >= bits->size) return -84;
        value |= ((bits->data[bits->byte] >> bits->bit) & 1U) << i;
        if (++bits->bit == 8U) { bits->bit = 0U; ++bits->byte; }
    }
    *code = (uint16_t)value;
    return 0;
}

static int gif_collect(const uint8_t *b, size_t n, size_t *offset,
                       uint8_t *packed, size_t capacity, size_t *used) {
    size_t count = 0U;
    for (;;) {
        if (*offset >= n) return -84;
        uint32_t block = b[(*offset)++];
        if (block == 0U) { *used = count; return 0; }
        if (block > n - *offset || block > capacity - count) return -75;
        for (uint32_t i = 0U; i < block; ++i) packed[count++] = b[(*offset)++];
    }
}

static void gif_position(uint32_t index, uint32_t width, uint32_t height,
                         uint32_t interlaced, uint32_t *x, uint32_t *y) {
    *x = index % width;
    uint32_t row = index / width;
    if (!interlaced) { *y = row; return; }
    static const uint8_t starts[4] = {0U, 4U, 2U, 1U};
    static const uint8_t steps[4] = {8U, 8U, 4U, 2U};
    for (uint32_t pass = 0U; pass < 4U; ++pass) {
        uint32_t rows = height > starts[pass]
            ? (height - starts[pass] + steps[pass] - 1U) / steps[pass] : 0U;
        if (row < rows) { *y = starts[pass] + row * steps[pass]; return; }
        row -= rows;
    }
    *y = height;
}

static int gif_lzw(const uint8_t *data, size_t size, uint32_t minimum,
                   const uint32_t *palette, uint32_t palette_size,
                   int transparent, uint32_t *pixels, uint32_t canvas_width,
                   uint32_t left, uint32_t top, uint32_t width,
                   uint32_t height, uint32_t interlaced,
                   reist_image_workspace_t *workspace) {
    if (minimum < 2U || minimum > 8U) return -95;
    uint16_t *prefix = workspace->prefix;
    uint8_t *suffix = workspace->suffix;
    uint8_t *stack = workspace->stack;
    uint16_t clear = (uint16_t)(1U << minimum), end = clear + 1U;
    uint16_t next = end + 1U, old = UINT16_MAX;
    uint32_t code_width = minimum + 1U, output = 0U;
    uint8_t first = 0U;
    for (uint16_t i = 0U; i < clear; ++i) suffix[i] = (uint8_t)i;
    gif_bits_t bits = {data, size, 0U, 0U};
    while (output < width * height) {
        uint16_t code;
        if (gif_code(&bits, code_width, &code) != 0) return -84;
        if (code == clear) { next = end + 1U; code_width = minimum + 1U;
            old = UINT16_MAX; continue; }
        if (code == end) break;
        uint16_t current = code;
        uint32_t count = 0U;
        if (code >= next) {
            if (old == UINT16_MAX || code != next) return -84;
            stack[count++] = first; current = old;
        }
        while (current >= clear) {
            if (current >= next || count >= REIST_IMAGE_GIF_TABLE_CAPACITY)
                return -84;
            stack[count++] = suffix[current]; current = prefix[current];
        }
        first = suffix[current];
        if (count >= REIST_IMAGE_GIF_TABLE_CAPACITY) return -84;
        stack[count++] = first;
        while (count != 0U && output < width * height) {
            uint32_t index = stack[--count], x, y;
            if (index >= palette_size) return -84;
            gif_position(output++, width, height, interlaced, &x, &y);
            if ((int)index != transparent && y < height)
                pixels[(size_t)(top + y) * canvas_width + left + x] = palette[index];
        }
        if (old != UINT16_MAX && next < REIST_IMAGE_GIF_TABLE_CAPACITY) {
            prefix[next] = old; suffix[next] = first; ++next;
            if (next == (1U << code_width) && code_width < 12U) ++code_width;
        }
        old = code;
    }
    return output == width * height ? 0 : -84;
}

static int decode_gif(const uint8_t *b, size_t n, uint32_t *pixels,
                      size_t capacity, reist_image_workspace_t *workspace,
                      reist_image_info_t *info) {
    if (n < 13U || b[0] != 'G' || b[1] != 'I' || b[2] != 'F' ||
        b[3] != '8' || (b[4] != '7' && b[4] != '9') || b[5] != 'a') return -84;
    uint32_t canvas_width = le16(&b[6]), canvas_height = le16(&b[8]);
    if (!dimensions(canvas_width, canvas_height, capacity)) return -75;
    for (size_t i = 0U; i < (size_t)canvas_width * canvas_height; ++i)
        pixels[i] = 0x00FFFFFFU;
    size_t offset = 13U;
    uint32_t global[256], global_size = 0U;
    if ((b[10] & 0x80U) != 0U) {
        global_size = 1U << ((b[10] & 7U) + 1U);
        if ((size_t)global_size * 3U > n - offset) return -84;
        for (uint32_t i = 0U; i < global_size; ++i) {
            global[i] = ((uint32_t)b[offset] << 16U) |
                ((uint32_t)b[offset + 1U] << 8U) | b[offset + 2U]; offset += 3U;
        }
    }
    int transparent = -1;
    uint32_t frames = 0U;
    uint8_t *packed = workspace->bytes;
    while (offset < n) {
        uint8_t marker = b[offset++];
        if (marker == 0x3BU) break;
        if (marker == 0x21U) {
            if (offset >= n) return -84;
            uint8_t label = b[offset++];
            if (label == 0xF9U) {
                if (offset + 6U > n || b[offset] != 4U) return -84;
                transparent = (b[offset + 1U] & 1U) ? b[offset + 4U] : -1;
                offset += 6U;
            } else {
                size_t ignored;
                if (gif_collect(b, n, &offset, packed,
                                REIST_IMAGE_WORKSPACE_BYTES, &ignored) != 0)
                    return -84;
            }
            continue;
        }
        if (marker != 0x2CU || offset + 9U > n) return -84;
        uint32_t left = le16(&b[offset]), top = le16(&b[offset + 2U]);
        uint32_t width = le16(&b[offset + 4U]), height = le16(&b[offset + 6U]);
        uint8_t flags = b[offset + 8U]; offset += 9U;
        if (width == 0U || height == 0U || left + width > canvas_width ||
            top + height > canvas_height) return -84;
        uint32_t local[256], local_size = 0U;
        const uint32_t *palette = global; uint32_t palette_size = global_size;
        if ((flags & 0x80U) != 0U) {
            local_size = 1U << ((flags & 7U) + 1U);
            if ((size_t)local_size * 3U > n - offset) return -84;
            for (uint32_t i = 0U; i < local_size; ++i) {
                local[i] = ((uint32_t)b[offset] << 16U) |
                    ((uint32_t)b[offset + 1U] << 8U) | b[offset + 2U]; offset += 3U;
            }
            palette = local; palette_size = local_size;
        }
        if (palette_size == 0U || offset >= n) return -84;
        uint32_t minimum = b[offset++]; size_t used;
        int result = gif_collect(b, n, &offset, packed,
                                 REIST_IMAGE_WORKSPACE_BYTES, &used);
        if (result != 0) return result;
        ++frames;
        if (frames == 1U) {
            result = gif_lzw(packed, used, minimum, palette, palette_size,
                transparent, pixels, canvas_width, left, top, width, height,
                (flags & 0x40U) != 0U, workspace);
            if (result != 0) return result;
        }
        transparent = -1;
    }
    if (frames == 0U) return -84;
    publish(info, canvas_width, canvas_height, REIST_IMAGE_FORMAT_GIF,
            frames, frames > 1U ? 1U : 0U);
    return 0;
}

int reist_image_decode(const uint8_t *encoded, size_t encoded_size,
                       uint32_t *pixels, size_t pixel_capacity,
                       reist_image_workspace_t *workspace,
                       reist_image_info_t *info) {
    if (encoded == 0 || pixels == 0 || workspace == 0 || info == 0 ||
        encoded_size < 2U)
        return -22;
    if (encoded[0] == 'B' && encoded[1] == 'M')
        return decode_bmp(encoded, encoded_size, pixels, pixel_capacity, info);
    if (encoded_size >= 6U && encoded[0] == 'G' && encoded[1] == 'I' &&
        encoded[2] == 'F')
        return decode_gif(encoded, encoded_size, pixels, pixel_capacity,
                          workspace, info);
    return -95;
}
