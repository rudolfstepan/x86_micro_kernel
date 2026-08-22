/** @file image_ico.c @brief Strict fixed-bound Microsoft ICO DIB decoder. */
#include "reist/image.h"

#include <limits.h>

#define ICO_DIRECTORY_HEADER_BYTES 6U
#define ICO_DIRECTORY_ENTRY_BYTES 16U
#define ICO_MAX_DIRECTORY_ENTRIES 16U
#define ICO_DIB_HEADER_BYTES 40U
#define ICO_MAX_DIMENSION 32U

typedef struct ico_candidate {
    uint32_t width;
    uint32_t height;
    size_t pixel_offset;
    size_t mask_offset;
    size_t mask_row_bytes;
} ico_candidate_t;

static uint16_t ico_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
}

static uint32_t ico_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
        ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static int ico_is_png(const uint8_t *bytes, size_t size) {
    static const uint8_t signature[8] = {
        0x89U, 'P', 'N', 'G', 0x0DU, 0x0AU, 0x1AU, 0x0AU
    };
    if (size < sizeof(signature)) return 0;
    for (size_t index = 0U; index < sizeof(signature); ++index)
        if (bytes[index] != signature[index]) return 0;
    return 1;
}

static int ico_validate_candidate(const uint8_t *encoded, size_t encoded_size,
                                  size_t directory_size, size_t entry_offset,
                                  ico_candidate_t *candidate) {
    uint32_t width = encoded[entry_offset] == 0U
        ? 256U : encoded[entry_offset];
    uint32_t height = encoded[entry_offset + 1U] == 0U
        ? 256U : encoded[entry_offset + 1U];
    uint16_t planes = ico_le16(&encoded[entry_offset + 4U]);
    uint16_t bits = ico_le16(&encoded[entry_offset + 6U]);
    uint32_t payload_size = ico_le32(&encoded[entry_offset + 8U]);
    uint32_t payload_offset = ico_le32(&encoded[entry_offset + 12U]);
    if ((width != 16U && width != ICO_MAX_DIMENSION) || height != width ||
        planes != 1U || bits != 32U) return 0;
    if ((size_t)payload_offset < directory_size ||
        (size_t)payload_offset > encoded_size ||
        (size_t)payload_size > encoded_size - (size_t)payload_offset)
        return -84;
    const uint8_t *payload = &encoded[payload_offset];
    if (ico_is_png(payload, payload_size)) return -95;
    if (payload_size < ICO_DIB_HEADER_BYTES ||
        ico_le32(payload) != ICO_DIB_HEADER_BYTES) return -95;
    int32_t dib_width = (int32_t)ico_le32(&payload[4]);
    int32_t dib_height = (int32_t)ico_le32(&payload[8]);
    if (dib_width != (int32_t)width ||
        dib_height != (int32_t)(height * 2U) ||
        ico_le16(&payload[12]) != 1U || ico_le16(&payload[14]) != 32U ||
        ico_le32(&payload[16]) != 0U) return -95;

    size_t pixel_bytes = (size_t)width * height * 4U;
    size_t mask_row = ((size_t)width + 31U) / 32U * 4U;
    size_t mask_bytes = mask_row * height;
    size_t expected = ICO_DIB_HEADER_BYTES + pixel_bytes + mask_bytes;
    uint32_t declared_image_size = ico_le32(&payload[20]);
    if ((size_t)payload_size != expected ||
        (declared_image_size != 0U &&
         (size_t)declared_image_size != pixel_bytes + mask_bytes)) return -84;
    candidate->width = width;
    candidate->height = height;
    candidate->pixel_offset = (size_t)payload_offset + ICO_DIB_HEADER_BYTES;
    candidate->mask_offset = candidate->pixel_offset + pixel_bytes;
    candidate->mask_row_bytes = mask_row;
    return 1;
}

int reist_image_decode_ico(const uint8_t *encoded, size_t encoded_size,
                           uint32_t *pixels, size_t pixel_capacity,
                           reist_image_info_t *info) {
    if (encoded == 0 || pixels == 0 || info == 0 ||
        encoded_size < ICO_DIRECTORY_HEADER_BYTES) return -22;
    uint16_t count = ico_le16(&encoded[4]);
    if (ico_le16(encoded) != 0U || ico_le16(&encoded[2]) != 1U ||
        count == 0U || count > ICO_MAX_DIRECTORY_ENTRIES) return -95;
    size_t directory_size = ICO_DIRECTORY_HEADER_BYTES +
        (size_t)count * ICO_DIRECTORY_ENTRY_BYTES;
    if (directory_size > encoded_size) return -84;

    ico_candidate_t selected = {0};
    uint32_t candidates = 0U;
    for (uint32_t index = 0U; index < count; ++index) {
        ico_candidate_t candidate;
        int result = ico_validate_candidate(
            encoded, encoded_size, directory_size,
            ICO_DIRECTORY_HEADER_BYTES +
                (size_t)index * ICO_DIRECTORY_ENTRY_BYTES,
            &candidate);
        if (result < 0) return result;
        if (result == 1) {
            if (++candidates != 1U) return -84;
            selected = candidate;
        }
    }
    if (candidates != 1U) return -95;
    size_t pixel_count = (size_t)selected.width * selected.height;
    if (pixel_count > pixel_capacity) return -75;

    uint32_t explicit_alpha = 0U;
    for (size_t index = 0U; index < pixel_count; ++index)
        if (encoded[selected.pixel_offset + index * 4U + 3U] != 0U) {
            explicit_alpha = 1U;
            break;
        }
    for (uint32_t y = 0U; y < selected.height; ++y) {
        uint32_t source_y = selected.height - 1U - y;
        size_t source_row = selected.pixel_offset +
            (size_t)source_y * selected.width * 4U;
        size_t mask_row = selected.mask_offset +
            (size_t)source_y * selected.mask_row_bytes;
        for (uint32_t x = 0U; x < selected.width; ++x) {
            size_t source = source_row + (size_t)x * 4U;
            uint32_t masked = (encoded[mask_row + x / 8U] &
                               (uint8_t)(0x80U >> (x & 7U))) != 0U;
            uint32_t alpha = explicit_alpha ? encoded[source + 3U] : 0xFFU;
            if (masked) alpha = 0U;
            pixels[(size_t)y * selected.width + x] =
                (alpha << 24U) | ((uint32_t)encoded[source + 2U] << 16U) |
                ((uint32_t)encoded[source + 1U] << 8U) | encoded[source];
        }
    }
    *info = (reist_image_info_t){
        REIST_IMAGE_API_VERSION, sizeof(*info), selected.width,
        selected.height, selected.width, REIST_IMAGE_FORMAT_ICO, 1U,
        REIST_IMAGE_FLAG_ALPHA
    };
    return 0;
}
