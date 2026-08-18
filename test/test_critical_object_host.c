/**
 * @file test/test_critical_object_host.c
 * @brief Hostseitiger Regressionstest für critical object.
 *
 * Layer: Host test harness.
 * Contract: Prüft beobachtbares Verhalten und feste Fehlergrenzen ohne Zielhardware.
 * Safety: Testdoubles dürfen Produktionsverträge nicht abschwächen oder Erfolg vortäuschen.
 */
#include "include/kernel/critical_object.h"

#include <stdint.h>

typedef struct { uint32_t limit; uint32_t mode; } sample_t;

static bool valid_sample(const void *payload, size_t length) {
    if (length != sizeof(sample_t)) return false;
    const sample_t *sample = (const sample_t *)payload;
    return sample->limit <= 1000U && sample->mode <= 3U;
}

int main(void) {
    critical_object_t object;
    sample_t input = {500U, 2U}, output = {0, 0};
    size_t length = 0;
    if (critical_object_init(&object, 1U, &input, sizeof(input)) != 0) return 1;
    if (critical_object_read(&object, 1U, &output, sizeof(output), &length,
                             valid_sample) != CRITICAL_READ_OK) return 2;
    if (output.limit != 500U || output.mode != 2U) return 3;

    object.primary.words[CRITICAL_OBJECT_METADATA_WORDS] ^= 1U << 7;
    if (critical_object_read(&object, 1U, &output, sizeof(output), &length,
                             valid_sample) != CRITICAL_READ_CORRECTED) return 4;
    if (output.limit != 500U) return 5;

    object.primary.words[CRITICAL_OBJECT_METADATA_WORDS] ^= 3U;
    if (critical_object_read(&object, 1U, &output, sizeof(output), &length,
                             valid_sample) != CRITICAL_READ_RECOVERED) return 6;
    if (output.limit != 500U) return 7;

    input.limit = 750U;
    if (critical_object_update(&object, 1U, &input, sizeof(input),
                               valid_sample) != 0) return 8;
    if (critical_object_read(&object, 1U, &output, sizeof(output), &length,
                             valid_sample) != CRITICAL_READ_OK) return 9;
    if (output.limit != 750U) return 10;

    object.primary.crc32 ^= 1U;
    object.shadow.crc32 ^= 2U;
    if (critical_object_read(&object, 1U, &output, sizeof(output), &length,
                             valid_sample) != CRITICAL_READ_UNCORRECTABLE) return 11;
    return 0;
}
