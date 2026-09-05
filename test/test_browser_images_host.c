#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "browser_images.h"
static uint8_t encoded[BROWSER_IMAGE_INPUT_LIMIT];
static struct { uint32_t before; uint32_t pixels[BROWSER_IMAGE_PIXEL_LIMIT]; uint32_t after; } output;
static reist_image_info_t info;
static uint64_t arena[BROWSER_DECODE_ARENA_BYTES / sizeof(uint64_t)];
int main(int argc, char **argv) {
    assert(argc == 5);
    assert(browser_image_workspace(arena, sizeof(arena)) == 0);
    output.before = output.after = 0x1234ABCDU;
    for (int file = 1; file < argc; ++file) {
        FILE *input = fopen(argv[file], "rb"); assert(input);
        size_t n = fread(encoded, 1, sizeof(encoded), input); fclose(input);
        assert(n >= 8 && n < sizeof(encoded));
        assert(browser_image_decode(encoded, n, output.pixels, BROWSER_IMAGE_PIXEL_LIMIT, &info) == 0);
        assert(info.width && info.height && info.stride_pixels == info.width);
        if (file == 1) { assert(info.width == 2 && info.height == 1); assert(output.pixels[0] == 0xFF0000); assert(output.pixels[1] == 0x7FFF7F); }
        if (file == 2) { assert(info.width == 1 && info.height == 1); assert(output.pixels[0] == 0x808080); }
        if (file == 3) { assert(info.width == 1 && info.height == 1); assert(output.pixels[0] == 0x0000FF); }
        assert(browser_image_decode(encoded, n, output.pixels, 0, &info) < 0);
        /* Every truncation for small fixtures; bounded prefix sampling for GIF. */
        for (size_t end = 0; end < n; end += n > 1024 ? n / 64 : 1) {
            (void)browser_image_decode(encoded, end, output.pixels, BROWSER_IMAGE_PIXEL_LIMIT, &info);
            assert(output.before == 0x1234ABCDU && output.after == 0x1234ABCDU);
        }
        for (size_t byte = 0; byte < n && byte < 512; ++byte) {
            encoded[byte] ^= 0x80;
            (void)browser_image_decode(encoded, n, output.pixels, BROWSER_IMAGE_PIXEL_LIMIT, &info);
            encoded[byte] ^= 0x80;
            assert(output.before == 0x1234ABCDU && output.after == 0x1234ABCDU);
        }
        /* Failed decoding cannot consume the next call's arena. */
        assert(browser_image_decode(encoded, n, output.pixels, BROWSER_IMAGE_PIXEL_LIMIT, &info) == 0);
        if (file == 1) { encoded[n - 1] ^= 1; assert(browser_image_decode(encoded, n, output.pixels, BROWSER_IMAGE_PIXEL_LIMIT, &info) < 0); }
    }
    assert(browser_image_decode(encoded, BROWSER_IMAGE_INPUT_LIMIT + 1U, output.pixels, BROWSER_IMAGE_PIXEL_LIMIT, &info) < 0);
    return 0;
}
