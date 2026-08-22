#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "reist/image.h"

#define ICO_BUFFER_CAPACITY 5000U

static void put16(uint8_t *bytes, uint32_t offset, uint16_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1U] = (uint8_t)(value >> 8U);
}

static void put32(uint8_t *bytes, uint32_t offset, uint32_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1U] = (uint8_t)(value >> 8U);
    bytes[offset + 2U] = (uint8_t)(value >> 16U);
    bytes[offset + 3U] = (uint8_t)(value >> 24U);
}

static uint32_t make_ico(uint8_t *bytes, uint32_t width, uint32_t count) {
    memset(bytes, 0, ICO_BUFFER_CAPACITY);
    uint32_t xor_size = width * width * 4U;
    uint32_t mask_row = ((width + 31U) / 32U) * 4U;
    uint32_t payload_size = 40U + xor_size + mask_row * width;
    uint32_t payload_offset = 6U + count * 16U;
    put16(bytes, 2U, 1U);
    put16(bytes, 4U, (uint16_t)count);
    for (uint32_t index = 0U; index < count; ++index) {
        uint32_t entry = 6U + index * 16U;
        bytes[entry] = (uint8_t)width;
        bytes[entry + 1U] = (uint8_t)width;
        put16(bytes, entry + 4U, 1U);
        put16(bytes, entry + 6U, 32U);
        put32(bytes, entry + 8U, payload_size);
        put32(bytes, entry + 12U, payload_offset);
    }
    put32(bytes, payload_offset, 40U);
    put32(bytes, payload_offset + 4U, width);
    put32(bytes, payload_offset + 8U, width * 2U);
    put16(bytes, payload_offset + 12U, 1U);
    put16(bytes, payload_offset + 14U, 32U);
    put32(bytes, payload_offset + 20U, xor_size + mask_row * width);
    for (uint32_t pixel = 0U; pixel < width * width; ++pixel) {
        uint32_t offset = payload_offset + 40U + pixel * 4U;
        bytes[offset] = 0x11U;
        bytes[offset + 1U] = 0x22U;
        bytes[offset + 2U] = 0x33U;
        bytes[offset + 3U] = 0U;
    }
    return payload_offset + payload_size;
}

static void test_valid_legacy_alpha_and_mask(void) {
    uint8_t encoded[ICO_BUFFER_CAPACITY];
    uint32_t pixels[32U * 32U];
    reist_image_info_t info = {0};
    uint32_t size = make_ico(encoded, 16U, 1U);
    assert(reist_image_decode_ico(encoded, size, pixels,
                                 32U * 32U, &info) == 0);
    assert(info.width == 16U && info.height == 16U);
    assert(info.format == REIST_IMAGE_FORMAT_ICO);
    assert((info.flags & REIST_IMAGE_FLAG_ALPHA) != 0U);
    assert(pixels[0] == 0xFF332211U);

    uint32_t payload = 22U;
    uint32_t mask = payload + 40U + 16U * 16U * 4U;
    encoded[mask + 16U * 4U - 4U] = 0x80U;
    assert(reist_image_decode_ico(encoded, size, pixels,
                                 32U * 32U, &info) == 0);
    assert(pixels[0] == 0x00332211U);
}

static void test_rejects_unsupported_and_ambiguous_inputs(void) {
    uint8_t encoded[ICO_BUFFER_CAPACITY];
    uint32_t pixels[32U * 32U];
    reist_image_info_t info;
    uint32_t size = make_ico(encoded, 32U, 1U);
    uint32_t payload = 22U;

    memset(&info, 0xA5, sizeof(info));
    memcpy(&encoded[payload], "\x89PNG\r\n\x1a\n", 8U);
    assert(reist_image_decode_ico(encoded, size, pixels,
                                 32U * 32U, &info) != 0);
    assert(info.width == 0xA5A5A5A5U);

    size = make_ico(encoded, 32U, 1U);
    put32(encoded, payload + 16U, 1U);
    assert(reist_image_decode_ico(encoded, size, pixels,
                                 32U * 32U, &info) != 0);

    size = make_ico(encoded, 32U, 1U);
    put32(encoded, 18U, size + 1U);
    assert(reist_image_decode_ico(encoded, size, pixels,
                                 32U * 32U, &info) != 0);

    size = make_ico(encoded, 16U, 2U);
    assert(reist_image_decode_ico(encoded, size, pixels,
                                 32U * 32U, &info) != 0);
}

static void test_general_dispatch(void) {
    uint8_t encoded[ICO_BUFFER_CAPACITY];
    uint32_t pixels[32U * 32U];
    reist_image_workspace_t workspace;
    reist_image_info_t info;
    uint32_t size = make_ico(encoded, 32U, 1U);
    assert(reist_image_decode(encoded, size, pixels, 32U * 32U,
                             &workspace, &info) == 0);
    assert(info.format == REIST_IMAGE_FORMAT_ICO && info.width == 32U);
}

int main(void) {
    test_valid_legacy_alpha_and_mask();
    test_rejects_unsupported_and_ambiguous_inputs();
    test_general_dispatch();
    return 0;
}
