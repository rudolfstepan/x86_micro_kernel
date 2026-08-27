/**
 * @file kernel/init/critical_object.c
 * @brief Implementiert redundante Integritätskopien für kleinen kritischen Zustand.
 *
 * Layer: Ring-0 integrity primitive.
 * Contract: Publikation und Auswahl verwenden Version, Sequenz und CRC.
 * Safety: Doppelkorruption wird gemeldet; die Nutzlast ist fest begrenzt.
 */
#include "include/kernel/critical_object.h"

#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
#include "arch/x86/include/interrupt.h"
#endif

#define WORD_VERSION 0U
#define WORD_SEQUENCE 1U
#define WORD_LENGTH 2U
#define WORD_INTEGRITY 3U
#define WORD_PAYLOAD 4U
#define CRITICAL_OBJECT_LOCK_RETRY_LIMIT (1U << 20U)

typedef enum { COPY_INVALID = -1, COPY_VALID = 0, COPY_CORRECTED = 1 } copy_status_t;

static bool critical_object_lock(critical_object_t *object,
                                 uint32_t *irq_flags_out) {
    if (object == 0 || irq_flags_out == 0) return false;
#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
    *irq_flags_out = irq_save();
#else
    *irq_flags_out = 0U;
#endif
    for (uint32_t retry = 0U; retry < CRITICAL_OBJECT_LOCK_RETRY_LIMIT;
         ++retry) {
        if (__sync_bool_compare_and_swap(&object->publication_lock, 0U, 1U)) {
            __sync_synchronize();
            return true;
        }
        __asm__ __volatile__("pause");
    }
#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
    irq_restore(*irq_flags_out);
#endif
    return false;
}

static void critical_object_unlock(critical_object_t *object,
                                   uint32_t irq_flags) {
    __sync_synchronize();
    __sync_lock_release(&object->publication_lock);
#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
    irq_restore(irq_flags);
#else
    (void)irq_flags;
#endif
}

static bool parity32(uint32_t value) {
    value ^= value >> 16;
    value ^= value >> 8;
    value ^= value >> 4;
    value &= 0xFU;
    return ((0x6996U >> value) & 1U) != 0;
}

static bool parity8(uint8_t value) {
    return parity32(value);
}

static bool parity_position(uint32_t position) {
    return position != 0 && (position & (position - 1U)) == 0;
}

static uint8_t secded_encode(uint32_t data) {
    uint8_t parity = 0;
    uint32_t data_bit = 0;
    for (uint32_t position = 1; position <= 38U; ++position) {
        if (parity_position(position)) continue;
        if (((data >> data_bit++) & 1U) != 0) parity ^= (uint8_t)position;
    }
    uint8_t code = parity & 0x3FU;
    bool overall = parity32(data) ^ parity8(code);
    return (uint8_t)(code | ((uint8_t)overall << 6));
}

static int secded_decode(uint32_t *data, uint8_t stored) {
    if ((stored & 0x80U) != 0) return -1;
    uint8_t syndrome = stored & 0x3FU;
    uint32_t data_bit = 0;
    for (uint32_t position = 1; position <= 38U; ++position) {
        if (parity_position(position)) continue;
        if (((*data >> data_bit++) & 1U) != 0) syndrome ^= (uint8_t)position;
    }
    bool overall_mismatch = parity32(*data) ^ parity8(stored & 0x3FU) ^
                            (((stored >> 6) & 1U) != 0);
    if (syndrome == 0 && !overall_mismatch) return 0;
    if (syndrome != 0 && !overall_mismatch) return -1;
    if (syndrome != 0) {
        if (syndrome > 38U) return -1;
        if (!parity_position(syndrome)) {
            uint32_t index = 0;
            for (uint32_t position = 1; position <= 38U; ++position) {
                if (parity_position(position)) continue;
                if (position == syndrome) {
                    *data ^= 1U << index;
                    break;
                }
                ++index;
            }
        }
    }
    return 1;
}

static uint32_t crc32_bytes(const void *data, size_t length, uint32_t crc) {
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t index = 0; index < length; ++index) {
        crc ^= bytes[index];
        for (uint32_t bit = 0; bit < 8U; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return crc;
}

static uint32_t copy_crc(const critical_object_copy_t *copy) {
    uint32_t crc = crc32_bytes(copy->words, sizeof(copy->words), 0xFFFFFFFFU);
    return crc32_bytes(copy->ecc, sizeof(copy->ecc), crc) ^ 0xFFFFFFFFU;
}

static void bytes_copy(void *destination, const void *source, size_t length) {
    uint8_t *to = (uint8_t *)destination;
    const uint8_t *from = (const uint8_t *)source;
    for (size_t index = 0; index < length; ++index) to[index] = from[index];
}

static void copy_seal(critical_object_copy_t *copy) {
    for (uint32_t index = 0; index < CRITICAL_OBJECT_WORDS; ++index) {
        copy->ecc[index] = secded_encode(copy->words[index]);
    }
    copy->crc32 = copy_crc(copy);
}

static void copy_build(critical_object_copy_t *copy, uint32_t version,
                       uint32_t sequence, critical_integrity_state_t state,
                       const void *payload, size_t length) {
    for (uint32_t index = 0; index < CRITICAL_OBJECT_WORDS; ++index) copy->words[index] = 0;
    copy->words[WORD_VERSION] = version;
    copy->words[WORD_SEQUENCE] = sequence;
    copy->words[WORD_LENGTH] = (uint32_t)length;
    copy->words[WORD_INTEGRITY] = (uint32_t)state;
    bytes_copy(&copy->words[WORD_PAYLOAD], payload, length);
    copy_seal(copy);
}

static copy_status_t copy_check(critical_object_copy_t *copy,
                                uint32_t expected_version,
                                critical_object_validator_t validator) {
    bool corrected = false;
    for (uint32_t index = 0; index < CRITICAL_OBJECT_WORDS; ++index) {
        int status = secded_decode(&copy->words[index], copy->ecc[index]);
        if (status < 0) return COPY_INVALID;
        corrected |= status > 0;
        copy->ecc[index] = secded_encode(copy->words[index]);
    }
    if (copy_crc(copy) != copy->crc32 ||
        copy->words[WORD_VERSION] != expected_version ||
        copy->words[WORD_LENGTH] > CRITICAL_OBJECT_MAX_PAYLOAD ||
        copy->words[WORD_INTEGRITY] > CRITICAL_INTEGRITY_RECOVERED) return COPY_INVALID;
    if (validator != 0 && !validator(&copy->words[WORD_PAYLOAD],
                                     copy->words[WORD_LENGTH])) return COPY_INVALID;
    return corrected ? COPY_CORRECTED : COPY_VALID;
}

static bool copies_equal(const critical_object_copy_t *left,
                         const critical_object_copy_t *right) {
    if (left->words[WORD_VERSION] != right->words[WORD_VERSION] ||
        left->words[WORD_SEQUENCE] != right->words[WORD_SEQUENCE] ||
        left->words[WORD_LENGTH] != right->words[WORD_LENGTH]) return false;
    size_t length = left->words[WORD_LENGTH];
    const uint8_t *a = (const uint8_t *)&left->words[WORD_PAYLOAD];
    const uint8_t *b = (const uint8_t *)&right->words[WORD_PAYLOAD];
    for (size_t index = 0; index < length; ++index) if (a[index] != b[index]) return false;
    return true;
}

int critical_object_init(critical_object_t *object, uint32_t version,
                         const void *payload, size_t length) {
    if (object == 0 || payload == 0 || version == 0 ||
        length == 0 || length > CRITICAL_OBJECT_MAX_PAYLOAD) return -1;
    object->publication_lock = 0U;
    __sync_synchronize();
    copy_build(&object->primary, version, 1U, CRITICAL_INTEGRITY_HEALTHY,
               payload, length);
    object->shadow = object->primary;
    return 0;
}

int critical_object_update(critical_object_t *object, uint32_t expected_version,
                           const void *payload, size_t length,
                           critical_object_validator_t validator) {
    if (object == 0 || payload == 0 || length == 0 ||
        length > CRITICAL_OBJECT_MAX_PAYLOAD || expected_version == 0) return -1;
    uint32_t irq_flags;
    if (!critical_object_lock(object, &irq_flags)) return -1;
    critical_object_copy_t primary = object->primary;
    critical_object_copy_t shadow = object->shadow;
    copy_status_t primary_status = copy_check(&primary, expected_version, validator);
    copy_status_t shadow_status = copy_check(&shadow, expected_version, validator);
    if (primary_status < 0 && shadow_status < 0) {
        critical_object_unlock(object, irq_flags);
        return -1;
    }
    if (primary_status >= 0 && shadow_status >= 0 &&
        primary.words[WORD_SEQUENCE] == shadow.words[WORD_SEQUENCE] &&
        !copies_equal(&primary, &shadow)) {
        critical_object_unlock(object, irq_flags);
        return -1;
    }
    uint32_t current_sequence = primary_status >= 0
        ? primary.words[WORD_SEQUENCE] : shadow.words[WORD_SEQUENCE];
    if (shadow_status >= 0 && shadow.words[WORD_SEQUENCE] > current_sequence)
        current_sequence = shadow.words[WORD_SEQUENCE];
    uint32_t sequence = current_sequence + 1U;
    if (sequence == 0) {
        critical_object_unlock(object, irq_flags);
        return -1;
    }
    critical_object_copy_t candidate;
    copy_build(&candidate, expected_version, sequence, CRITICAL_INTEGRITY_HEALTHY,
               payload, length);
    /* Publish the shadow first.  A reset between these assignments leaves at
     * least one complete old or new generation; readers select the newest
     * independently valid copy and never combine partial payloads. */
    object->shadow = candidate;
    object->primary = candidate;
    critical_object_unlock(object, irq_flags);
    return 0;
}

critical_read_result_t critical_object_read(
    critical_object_t *object, uint32_t expected_version, void *payload_out,
    size_t capacity, size_t *length_out, critical_object_validator_t validator) {
    if (object == 0 || payload_out == 0 || length_out == 0 || expected_version == 0)
        return CRITICAL_READ_INVALID_ARGUMENT;
    uint32_t irq_flags;
    if (!critical_object_lock(object, &irq_flags))
        return CRITICAL_READ_UNCORRECTABLE;
    critical_object_copy_t primary = object->primary;
    critical_object_copy_t shadow = object->shadow;
    copy_status_t primary_status = copy_check(&primary, expected_version, validator);
    copy_status_t shadow_status = copy_check(&shadow, expected_version, validator);
    if (primary_status < 0 && shadow_status < 0) {
        critical_object_unlock(object, irq_flags);
        return CRITICAL_READ_UNCORRECTABLE;
    }

    critical_object_copy_t *chosen;
    critical_read_result_t result;
    if (primary_status >= 0 && shadow_status >= 0) {
        if (primary.words[WORD_SEQUENCE] == shadow.words[WORD_SEQUENCE] &&
            !copies_equal(&primary, &shadow)) {
            critical_object_unlock(object, irq_flags);
            return CRITICAL_READ_UNCORRECTABLE;
        }
        chosen = primary.words[WORD_SEQUENCE] >= shadow.words[WORD_SEQUENCE]
                     ? &primary : &shadow;
        result = (primary_status > 0 || shadow_status > 0)
                     ? CRITICAL_READ_CORRECTED : CRITICAL_READ_OK;
    } else {
        chosen = primary_status >= 0 ? &primary : &shadow;
        result = CRITICAL_READ_RECOVERED;
    }
    size_t length = chosen->words[WORD_LENGTH];
    if (length > capacity) {
        critical_object_unlock(object, irq_flags);
        return CRITICAL_READ_INVALID_ARGUMENT;
    }
    critical_integrity_state_t state = result == CRITICAL_READ_RECOVERED
        ? CRITICAL_INTEGRITY_RECOVERED
        : (result == CRITICAL_READ_CORRECTED ? CRITICAL_INTEGRITY_CORRECTED
                                             : CRITICAL_INTEGRITY_HEALTHY);
    copy_build(&object->primary, chosen->words[WORD_VERSION],
               chosen->words[WORD_SEQUENCE], state,
               &chosen->words[WORD_PAYLOAD], length);
    object->shadow = object->primary;
    bytes_copy(payload_out, &chosen->words[WORD_PAYLOAD], length);
    *length_out = length;
    critical_object_unlock(object, irq_flags);
    return result;
}
