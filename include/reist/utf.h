/**
 * @file include/reist/utf.h
 * @brief Bounded RFC 3629 UTF-8 and UTF-16 scalar conversion.
 *
 * The helpers use caller-owned fixed storage, reject malformed sequences
 * before publishing partial output, and never allocate or access global state.
 */
#ifndef REIST_UTF_H
#define REIST_UTF_H

#include <stddef.h>
#include <stdint.h>

static inline int reist_utf8_decode_one(const char *input, size_t length,
                                        size_t *consumed, uint32_t *scalar) {
    if (input == NULL || consumed == NULL || scalar == NULL || length == 0U)
        return 0;
    const uint8_t *bytes = (const uint8_t *)input;
    uint32_t value;
    size_t count;
    if (bytes[0] <= 0x7FU) {
        value = bytes[0];
        count = 1U;
    } else if (bytes[0] >= 0xC2U && bytes[0] <= 0xDFU) {
        if (length < 2U || (bytes[1] & 0xC0U) != 0x80U) return 0;
        value = ((uint32_t)(bytes[0] & 0x1FU) << 6U) |
                (uint32_t)(bytes[1] & 0x3FU);
        count = 2U;
    } else if (bytes[0] >= 0xE0U && bytes[0] <= 0xEFU) {
        if (length < 3U || (bytes[1] & 0xC0U) != 0x80U ||
            (bytes[2] & 0xC0U) != 0x80U ||
            (bytes[0] == 0xE0U && bytes[1] < 0xA0U) ||
            (bytes[0] == 0xEDU && bytes[1] >= 0xA0U)) return 0;
        value = ((uint32_t)(bytes[0] & 0x0FU) << 12U) |
                ((uint32_t)(bytes[1] & 0x3FU) << 6U) |
                (uint32_t)(bytes[2] & 0x3FU);
        count = 3U;
    } else if (bytes[0] >= 0xF0U && bytes[0] <= 0xF4U) {
        if (length < 4U || (bytes[1] & 0xC0U) != 0x80U ||
            (bytes[2] & 0xC0U) != 0x80U ||
            (bytes[3] & 0xC0U) != 0x80U ||
            (bytes[0] == 0xF0U && bytes[1] < 0x90U) ||
            (bytes[0] == 0xF4U && bytes[1] > 0x8FU)) return 0;
        value = ((uint32_t)(bytes[0] & 0x07U) << 18U) |
                ((uint32_t)(bytes[1] & 0x3FU) << 12U) |
                ((uint32_t)(bytes[2] & 0x3FU) << 6U) |
                (uint32_t)(bytes[3] & 0x3FU);
        count = 4U;
    } else {
        return 0;
    }
    if (value >= 0xD800U && value <= 0xDFFFU) return 0;
    *consumed = count;
    *scalar = value;
    return 1;
}

static inline int reist_utf8_to_utf16(const char *input, size_t input_bytes,
                                      uint16_t *output,
                                      size_t output_capacity,
                                      size_t *output_units) {
    if (input == NULL || output == NULL || output_units == NULL) return 0;
    size_t source = 0U;
    size_t target = 0U;
    while (source < input_bytes) {
        size_t consumed = 0U;
        uint32_t scalar = 0U;
        if (!reist_utf8_decode_one(input + source, input_bytes - source,
                                   &consumed, &scalar) || scalar == 0U)
            return 0;
        size_t required = scalar <= 0xFFFFU ? 1U : 2U;
        if (target > output_capacity || required > output_capacity - target)
            return 0;
        if (required == 1U) {
            output[target++] = (uint16_t)scalar;
        } else {
            scalar -= 0x10000U;
            output[target++] = (uint16_t)(0xD800U + (scalar >> 10U));
            output[target++] = (uint16_t)(0xDC00U + (scalar & 0x3FFU));
        }
        source += consumed;
    }
    *output_units = target;
    return 1;
}

static inline int reist_utf16_to_utf8(const uint16_t *input,
                                      size_t input_units, char *output,
                                      size_t output_capacity,
                                      size_t *output_bytes) {
    if (input == NULL || output == NULL || output_bytes == NULL ||
        output_capacity == 0U) return 0;
    size_t source = 0U;
    size_t target = 0U;
    while (source < input_units) {
        uint32_t scalar = input[source++];
        if (scalar >= 0xD800U && scalar <= 0xDBFFU) {
            if (source >= input_units || input[source] < 0xDC00U ||
                input[source] > 0xDFFFU) return 0;
            scalar = 0x10000U + ((scalar - 0xD800U) << 10U) +
                     ((uint32_t)input[source++] - 0xDC00U);
        } else if (scalar >= 0xDC00U && scalar <= 0xDFFFU) {
            return 0;
        }
        size_t required = scalar <= 0x7FU ? 1U :
                          scalar <= 0x7FFU ? 2U :
                          scalar <= 0xFFFFU ? 3U : 4U;
        if (target >= output_capacity ||
            required >= output_capacity - target) return 0;
        if (required == 1U) {
            output[target++] = (char)scalar;
        } else if (required == 2U) {
            output[target++] = (char)(0xC0U | (scalar >> 6U));
            output[target++] = (char)(0x80U | (scalar & 0x3FU));
        } else if (required == 3U) {
            output[target++] = (char)(0xE0U | (scalar >> 12U));
            output[target++] = (char)(0x80U | ((scalar >> 6U) & 0x3FU));
            output[target++] = (char)(0x80U | (scalar & 0x3FU));
        } else {
            output[target++] = (char)(0xF0U | (scalar >> 18U));
            output[target++] = (char)(0x80U | ((scalar >> 12U) & 0x3FU));
            output[target++] = (char)(0x80U | ((scalar >> 6U) & 0x3FU));
            output[target++] = (char)(0x80U | (scalar & 0x3FU));
        }
    }
    output[target] = '\0';
    *output_bytes = target;
    return 1;
}

#endif
