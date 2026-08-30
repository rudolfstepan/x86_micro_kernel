/**
 * @file include/reist/unicode_norm.h
 * @brief Fixed-storage Unicode 15.0.0 NFC and default case folding.
 */
#ifndef REIST_UNICODE_NORM_H
#define REIST_UNICODE_NORM_H

#include <stddef.h>
#include <stdint.h>
#include "utf.h"
#include "unicode_tables_15_0.h"

#define REIST_UNICODE_PATH_BYTES 255U
/* The pinned Unicode-15 canonical-decomposition graph has maximum mapping
 * width two and needs at most four pending scalars during a depth-first
 * expansion. Regression-check the complete generated table before accepting
 * a data update. */
#define REIST_UNICODE_DECOMPOSITION_PENDING_CAPACITY 4U

static inline const reist_unicode_mapping_t *reist_unicode_find_mapping(
        const reist_unicode_mapping_t *table, size_t count, uint32_t scalar) {
    size_t low = 0U;
    size_t high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (table[middle].scalar < scalar) low = middle + 1U;
        else high = middle;
    }
    return low < count && table[low].scalar == scalar ? &table[low] : NULL;
}

static inline uint8_t reist_unicode_combining_class(uint32_t scalar) {
    size_t low = 0U;
    size_t high = REIST_UNICODE_COMBINING_CLASS_COUNT;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (reist_unicode_combining_classes[middle].scalar < scalar)
            low = middle + 1U;
        else high = middle;
    }
    return low < REIST_UNICODE_COMBINING_CLASS_COUNT &&
           reist_unicode_combining_classes[low].scalar == scalar
        ? reist_unicode_combining_classes[low].value : 0U;
}

static inline int reist_unicode_append_decomposed(
        uint32_t scalar, uint32_t output[REIST_UNICODE_DECOMPOSED_CAPACITY],
        size_t *count) {
    enum {
        S_BASE = 0xAC00U, L_BASE = 0x1100U, V_BASE = 0x1161U,
        T_BASE = 0x11A7U, L_COUNT = 19U, V_COUNT = 21U, T_COUNT = 28U,
        N_COUNT = V_COUNT * T_COUNT, S_COUNT = L_COUNT * N_COUNT
    };
    if (output == NULL || count == NULL) return 0;
    uint32_t pending[REIST_UNICODE_DECOMPOSITION_PENDING_CAPACITY];
    size_t pending_count = 1U;
    pending[0] = scalar;
    while (pending_count != 0U) {
        uint32_t current = pending[--pending_count];
        if (current >= S_BASE && current < S_BASE + S_COUNT) {
            uint32_t index = current - S_BASE;
            uint32_t required = index % T_COUNT == 0U ? 2U : 3U;
            if (*count > REIST_UNICODE_DECOMPOSED_CAPACITY - required)
                return 0;
            output[(*count)++] = L_BASE + index / N_COUNT;
            output[(*count)++] = V_BASE + (index % N_COUNT) / T_COUNT;
            if (required == 3U)
                output[(*count)++] = T_BASE + index % T_COUNT;
            continue;
        }
        const reist_unicode_mapping_t *mapping = reist_unicode_find_mapping(
            reist_unicode_decompositions, REIST_UNICODE_DECOMPOSITION_COUNT,
            current);
        if (mapping != NULL) {
            if ((size_t)mapping->length >
                REIST_UNICODE_DECOMPOSITION_PENDING_CAPACITY - pending_count)
                return 0;
            for (uint8_t index = mapping->length; index > 0U; --index) {
                pending[pending_count++] = reist_unicode_decomposition_data[
                    mapping->offset + index - 1U];
            }
            continue;
        }
        if (*count >= REIST_UNICODE_DECOMPOSED_CAPACITY) return 0;
        output[(*count)++] = current;
    }
    return 1;
}

static inline void reist_unicode_canonical_order(uint32_t *scalars,
                                                  size_t count) {
    for (size_t index = 1U; index < count; ++index) {
        uint8_t current_class = reist_unicode_combining_class(scalars[index]);
        if (current_class == 0U) continue;
        size_t position = index;
        while (position > 0U) {
            uint8_t previous_class =
                reist_unicode_combining_class(scalars[position - 1U]);
            if (previous_class == 0U || previous_class <= current_class) break;
            uint32_t temporary = scalars[position - 1U];
            scalars[position - 1U] = scalars[position];
            scalars[position] = temporary;
            --position;
        }
    }
}

static inline uint32_t reist_unicode_compose_pair(uint32_t starter,
                                                   uint32_t combining) {
    enum {
        S_BASE = 0xAC00U, L_BASE = 0x1100U, V_BASE = 0x1161U,
        T_BASE = 0x11A7U, L_COUNT = 19U, V_COUNT = 21U, T_COUNT = 28U,
        N_COUNT = V_COUNT * T_COUNT, S_COUNT = L_COUNT * N_COUNT
    };
    if (starter >= L_BASE && starter < L_BASE + L_COUNT &&
        combining >= V_BASE && combining < V_BASE + V_COUNT)
        return S_BASE + ((starter - L_BASE) * V_COUNT +
                         combining - V_BASE) * T_COUNT;
    if (starter >= S_BASE && starter < S_BASE + S_COUNT &&
        (starter - S_BASE) % T_COUNT == 0U && combining > T_BASE &&
        combining < T_BASE + T_COUNT)
        return starter + combining - T_BASE;

    size_t low = 0U;
    size_t high = REIST_UNICODE_COMPOSITION_COUNT;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        const reist_unicode_composition_t *entry =
            &reist_unicode_compositions[middle];
        if (entry->starter < starter ||
            (entry->starter == starter && entry->combining < combining))
            low = middle + 1U;
        else high = middle;
    }
    if (low < REIST_UNICODE_COMPOSITION_COUNT &&
        reist_unicode_compositions[low].starter == starter &&
        reist_unicode_compositions[low].combining == combining)
        return reist_unicode_compositions[low].composed;
    return 0U;
}

static inline size_t reist_unicode_canonical_compose(uint32_t *scalars,
                                                      size_t count) {
    if (count == 0U) return 0U;
    size_t output = 1U;
    size_t starter_position = 0U;
    uint32_t starter = scalars[0U];
    uint8_t last_class = 0U;
    for (size_t index = 1U; index < count; ++index) {
        uint32_t current = scalars[index];
        uint8_t current_class = reist_unicode_combining_class(current);
        uint32_t composed = reist_unicode_compose_pair(starter, current);
        if (composed != 0U &&
            (last_class == 0U || last_class < current_class)) {
            scalars[starter_position] = composed;
            starter = composed;
            continue;
        }
        if (current_class == 0U) {
            starter_position = output;
            starter = current;
        }
        last_class = current_class;
        scalars[output++] = current;
    }
    return output;
}

static inline int reist_unicode_append_utf8(uint32_t scalar, char *output,
                                             size_t capacity, size_t *used) {
    size_t required = scalar <= 0x7FU ? 1U : scalar <= 0x7FFU ? 2U :
                      scalar <= 0xFFFFU ? 3U : 4U;
    if (*used >= capacity || required >= capacity - *used) return 0;
    if (required == 1U) {
        output[(*used)++] = (char)scalar;
    } else if (required == 2U) {
        output[(*used)++] = (char)(0xC0U | (scalar >> 6U));
        output[(*used)++] = (char)(0x80U | (scalar & 0x3FU));
    } else if (required == 3U) {
        output[(*used)++] = (char)(0xE0U | (scalar >> 12U));
        output[(*used)++] = (char)(0x80U | ((scalar >> 6U) & 0x3FU));
        output[(*used)++] = (char)(0x80U | (scalar & 0x3FU));
    } else {
        output[(*used)++] = (char)(0xF0U | (scalar >> 18U));
        output[(*used)++] = (char)(0x80U | ((scalar >> 12U) & 0x3FU));
        output[(*used)++] = (char)(0x80U | ((scalar >> 6U) & 0x3FU));
        output[(*used)++] = (char)(0x80U | (scalar & 0x3FU));
    }
    return 1;
}

static inline int reist_unicode_nfc_casefold_key(
        const char *input, char output[REIST_UNICODE_KEY_CAPACITY]) {
    if (input == NULL || output == NULL) return 0;
    size_t input_bytes = 0U;
    while (input_bytes <= REIST_UNICODE_PATH_BYTES && input[input_bytes] != '\0')
        ++input_bytes;
    if (input_bytes == 0U || input_bytes > REIST_UNICODE_PATH_BYTES) return 0;

    uint32_t scalars[REIST_UNICODE_DECOMPOSED_CAPACITY];
    size_t scalar_count = 0U;
    size_t source = 0U;
    while (source < input_bytes) {
        size_t consumed = 0U;
        uint32_t scalar = 0U;
        if (!reist_utf8_decode_one(input + source, input_bytes - source,
                                   &consumed, &scalar) || scalar == 0U)
            return 0;
        const reist_unicode_mapping_t *fold = reist_unicode_find_mapping(
            reist_unicode_casefolds, REIST_UNICODE_CASEFOLD_COUNT, scalar);
        if (fold != NULL) {
            for (uint8_t index = 0U; index < fold->length; ++index)
                if (!reist_unicode_append_decomposed(
                        reist_unicode_casefold_data[fold->offset + index],
                        scalars, &scalar_count)) return 0;
        } else if (!reist_unicode_append_decomposed(
                       scalar, scalars, &scalar_count)) return 0;
        source += consumed;
    }
    reist_unicode_canonical_order(scalars, scalar_count);
    scalar_count = reist_unicode_canonical_compose(scalars, scalar_count);
    size_t used = 0U;
    for (size_t index = 0U; index < scalar_count; ++index)
        if (!reist_unicode_append_utf8(scalars[index], output,
                                       REIST_UNICODE_KEY_CAPACITY, &used))
            return 0;
    output[used] = '\0';
    return 1;
}

static inline int reist_unicode_caseless_nfc_equal(const char *left,
                                                    const char *right) {
    char left_key[REIST_UNICODE_KEY_CAPACITY];
    char right_key[REIST_UNICODE_KEY_CAPACITY];
    if (!reist_unicode_nfc_casefold_key(left, left_key) ||
        !reist_unicode_nfc_casefold_key(right, right_key)) return 0;
    size_t index = 0U;
    while (left_key[index] != '\0' && right_key[index] != '\0') {
        if (left_key[index] != right_key[index]) return 0;
        ++index;
    }
    return left_key[index] == right_key[index];
}

#endif
