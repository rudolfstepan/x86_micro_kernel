/** @file test_image_host.c @brief Behavior tests for libreistimage decoders. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

#include "reist/image.h"

static void test_bmp(void) {
    const uint8_t bmp[] = {
        'B','M', 62,0,0,0, 0,0,0,0, 54,0,0,0,
        40,0,0,0, 2,0,0,0, 1,0,0,0, 1,0, 24,0,
        0,0,0,0, 8,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,255, 0,255,0, 0,0
    };
    uint32_t pixels[2] = {0};
    reist_image_workspace_t workspace;
    reist_image_info_t info;
    assert(reist_image_decode(bmp, sizeof(bmp), pixels, 2U,
                              &workspace, &info) == 0);
    assert(info.width == 2U && info.height == 1U);
    assert(info.format == REIST_IMAGE_FORMAT_BMP);
    assert(pixels[0] == 0x00FF0000U && pixels[1] == 0x0000FF00U);
}

static void test_gif(void) {
    const uint8_t gif[] = {
        'G','I','F','8','7','a', 1,0, 1,0, 0x80,0,0,
        0,0,0, 255,255,255,
        0x2c, 0,0,0,0, 1,0,1,0, 0,
        2, 2, 0x44,0x01, 0, 0x3b
    };
    uint32_t pixel = 0xFFFFFFFFU;
    reist_image_workspace_t workspace;
    reist_image_info_t info;
    assert(reist_image_decode(gif, sizeof(gif), &pixel, 1U,
                              &workspace, &info) == 0);
    assert(info.width == 1U && info.height == 1U);
    assert(info.format == REIST_IMAGE_FORMAT_GIF && info.frame_count == 1U);
    assert(pixel == 0U);
}

static uint8_t file_bytes[1024U * 1024U];
static uint32_t file_pixels[REIST_IMAGE_MAX_WIDTH * REIST_IMAGE_MAX_HEIGHT];
static reist_image_workspace_t file_workspace;

static void test_file(const char *path, uint32_t format) {
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    size_t size = fread(file_bytes, 1U, sizeof(file_bytes), file);
    assert(size != 0U && !ferror(file));
    assert(fgetc(file) == EOF);
    assert(fclose(file) == 0);
    reist_image_info_t info;
    assert(reist_image_decode(file_bytes, size, file_pixels,
        REIST_IMAGE_MAX_WIDTH * REIST_IMAGE_MAX_HEIGHT,
        &file_workspace, &info) == 0);
    assert(info.format == format && info.width != 0U && info.height != 0U);
}

int main(int argc, char **argv) {
    test_bmp();
    test_gif();
    uint8_t invalid[6] = {'P','N','G',0,0,0};
    uint32_t pixel;
    reist_image_workspace_t workspace;
    reist_image_info_t info;
    assert(reist_image_decode(invalid, sizeof(invalid), &pixel, 1U,
                              &workspace, &info) == -95);
    if (argc == 3) {
        test_file(argv[1], REIST_IMAGE_FORMAT_BMP);
        test_file(argv[2], REIST_IMAGE_FORMAT_GIF);
    }
    return 0;
}
