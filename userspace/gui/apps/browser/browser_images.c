/* Browser-only bounded adapter. PNG: ISO/IEC 15948; JPEG: ITU-T T.81.
 * The upstream MIT decoder sees only immutable in-memory input and a fixed
 * arena. No allocations escape this call, including malformed-input exits. */
#include "browser_images.h"
#include <string.h>

#if !__STDC_HOSTED__
/* Browser-private ISO C byte operations for the freestanding toolchain.
 * The upstream decoder never receives the SDK allocator or device handles. */
void *memcpy(void *destination, const void *source, size_t count) {
    uint8_t *out = destination;
    const uint8_t *in = source;
    for (size_t i = 0; i < count; ++i) out[i] = in[i];
    return destination;
}
void *memset(void *destination, int value, size_t count) {
    uint8_t *out = destination;
    for (size_t i = 0; i < count; ++i) out[i] = (uint8_t)value;
    return destination;
}
int memcmp(const void *left, const void *right, size_t count) {
    const uint8_t *a = left, *b = right;
    for (size_t i = 0; i < count; ++i)
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    return 0;
}
#endif

static uint8_t *arena;
static size_t arena_used;
static reist_image_workspace_t legacy_workspace;

int browser_image_workspace(void *storage, size_t size) {
    if (storage == 0 || size != BROWSER_DECODE_ARENA_BYTES || ((uintptr_t)storage & 7U)) return -22;
    arena = storage; arena_used = 0U;
    return 0;
}

static void *decode_allocate(size_t size) {
    if (size == 0U || size > BROWSER_DECODE_ARENA_BYTES - 16U) return 0;
    size_t extent = (size + 15U) & ~(size_t)15U;
    if (extent > BROWSER_DECODE_ARENA_BYTES - arena_used) return 0;
    void *result = arena + arena_used;
    arena_used += extent;
    return result;
}
static void *decode_resize(void *old, size_t old_size, size_t new_size) {
    if (old != 0 && ((uintptr_t)old < (uintptr_t)arena ||
        (uintptr_t)old >= (uintptr_t)arena + arena_used ||
        old_size > (size_t)((uintptr_t)arena + arena_used - (uintptr_t)old))) return 0;
    void *result = decode_allocate(new_size);
    if (result != 0 && old != 0)
        memcpy(result, old, old_size < new_size ? old_size : new_size);
    return result;
}

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS 1024
#define STBI_MALLOC(size) decode_allocate(size)
#define STBI_REALLOC_SIZED(old, old_size, new_size) decode_resize(old, old_size, new_size)
#define STBI_FREE(pointer) ((void)(pointer))
#define STBI_ASSERT(condition) ((void)0)
#define STB_IMAGE_IMPLEMENTATION
#include "../../../../third_party/stb_image.h"

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) |
           ((uint32_t)p[2] << 8U) | p[3];
}
/* stb intentionally ignores PNG CRCs. The adapter rejects corrupt chunks
 * first, without publishing pixels; total CRC work is bounded by input size. */
static int png_envelope(const uint8_t *input, size_t length) {
    size_t at = 8U;
    uint32_t chunks = 0U;
    while (at <= length && length - at >= 12U) {
        uint32_t size = be32(input + at);
        if (size > length - at - 12U || ++chunks > 1024U) return 0;
        uint32_t crc = UINT32_MAX;
        for (size_t byte = at + 4U; byte < at + 8U + size; ++byte) {
            crc ^= input[byte];
            for (unsigned bit = 0; bit < 8U; ++bit)
                crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
        if ((crc ^ UINT32_MAX) != be32(input + at + 8U + size)) return 0;
        if (memcmp(input + at + 4U, "IEND", 4U) == 0)
            return size == 0U && at + 12U == length;
        at += size + 12U;
    }
    return 0;
}

int browser_image_decode(const uint8_t *encoded, size_t length,
                          uint32_t *pixels, size_t capacity,
                          reist_image_info_t *info) {
    if (arena == 0 || encoded == 0 || pixels == 0 || info == 0 || length < 8U ||
        length > BROWSER_IMAGE_INPUT_LIMIT) return -22;
    static const uint8_t signature[8] = {137,80,78,71,13,10,26,10};
    uint32_t png = memcmp(encoded, signature, 8U) == 0;
    uint32_t jpeg = encoded[0] == 255U && encoded[1] == 216U;
    if (!png && !jpeg)
        return reist_image_decode(encoded, length, pixels, capacity,
                                  &legacy_workspace, info);
    if ((png && !png_envelope(encoded, length)) ||
        (jpeg && (encoded[length - 2U] != 255U || encoded[length - 1U] != 217U)))
        return -84;
    arena_used = 0U;
    int width = 0, height = 0, components = 0;
    if (!stbi_info_from_memory(encoded, (int)length, &width, &height, &components) ||
        width <= 0 || height <= 0 || width > 1024 || height > 768 ||
        (size_t)width * (size_t)height > capacity) return -27;
    uint8_t *rgba = stbi_load_from_memory(encoded, (int)length,
                                         &width, &height, &components, 4);
    if (rgba == 0) { arena_used = 0U; return -84; }
    size_t count = (size_t)width * (size_t)height;
    if (count > capacity || count > BROWSER_IMAGE_PIXEL_LIMIT) {
        arena_used = 0U; return -27;
    }
    for (size_t index = 0U; index < count; ++index) {
        uint32_t alpha = rgba[index * 4U + 3U];
        uint32_t red = (rgba[index * 4U] * alpha + 255U * (255U - alpha) + 127U) / 255U;
        uint32_t green = (rgba[index * 4U + 1U] * alpha + 255U * (255U - alpha) + 127U) / 255U;
        uint32_t blue = (rgba[index * 4U + 2U] * alpha + 255U * (255U - alpha) + 127U) / 255U;
        pixels[index] = (red << 16U) | (green << 8U) | blue;
    }
    *info = (reist_image_info_t){REIST_IMAGE_API_VERSION, sizeof(*info),
        (uint32_t)width, (uint32_t)height, (uint32_t)width,
        0U /* browser-private format, not an image-library ABI extension */, 1U, 0U};
    arena_used = 0U;
    return 0;
}
