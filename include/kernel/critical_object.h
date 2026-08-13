#ifndef KERNEL_CRITICAL_OBJECT_H
#define KERNEL_CRITICAL_OBJECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CRITICAL_OBJECT_MAX_PAYLOAD 64U
#define CRITICAL_OBJECT_METADATA_WORDS 4U
#define CRITICAL_OBJECT_PAYLOAD_WORDS (CRITICAL_OBJECT_MAX_PAYLOAD / 4U)
#define CRITICAL_OBJECT_WORDS (CRITICAL_OBJECT_METADATA_WORDS + CRITICAL_OBJECT_PAYLOAD_WORDS)

typedef enum {
    CRITICAL_INTEGRITY_HEALTHY = 0,
    CRITICAL_INTEGRITY_CORRECTED = 1,
    CRITICAL_INTEGRITY_RECOVERED = 2,
    CRITICAL_INTEGRITY_UNCORRECTABLE = 3,
} critical_integrity_state_t;

typedef enum {
    CRITICAL_READ_OK = 0,
    CRITICAL_READ_CORRECTED = 1,
    CRITICAL_READ_RECOVERED = 2,
    CRITICAL_READ_UNCORRECTABLE = -1,
    CRITICAL_READ_INVALID_ARGUMENT = -2,
} critical_read_result_t;

typedef bool (*critical_object_validator_t)(const void *payload, size_t length);

typedef struct {
    uint32_t words[CRITICAL_OBJECT_WORDS];
    uint8_t ecc[CRITICAL_OBJECT_WORDS];
    uint32_t crc32;
} critical_object_copy_t;

typedef struct {
    critical_object_copy_t primary;
    critical_object_copy_t shadow;
} critical_object_t;

int critical_object_init(critical_object_t *object, uint32_t version,
                         const void *payload, size_t length);
int critical_object_update(critical_object_t *object, uint32_t expected_version,
                           const void *payload, size_t length,
                           critical_object_validator_t validator);
critical_read_result_t critical_object_read(
    critical_object_t *object, uint32_t expected_version, void *payload_out,
    size_t capacity, size_t *length_out, critical_object_validator_t validator);

#endif
