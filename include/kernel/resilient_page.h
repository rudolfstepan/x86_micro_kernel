/**
 * @file include/kernel/resilient_page.h
 * @brief Fixed-capacity experimental mirrored-page transaction contract.
 *
 * The domains in this first proof are software fault-injection labels.  They
 * do not claim physical DIMM, rank, channel or memory-controller isolation.
 */
#ifndef KERNEL_RESILIENT_PAGE_H
#define KERNEL_RESILIENT_PAGE_H

#include <stddef.h>
#include <stdint.h>

#define RESILIENT_PAGE_SIZE 4096U
#define RESILIENT_PAGE_CAPACITY 4U
#define RESILIENT_PAGE_DOMAIN_COUNT 3U
#define RESILIENT_PAGE_BANK_COUNT 2U

typedef enum {
    RESILIENT_PAGE_DOMAIN_A = 0,
    RESILIENT_PAGE_DOMAIN_B = 1,
    RESILIENT_PAGE_DOMAIN_C = 2,
} resilient_page_domain_t;

typedef enum {
    RESILIENT_PAGE_HEALTHY = 0,
    RESILIENT_PAGE_DEGRADED = 1,
    RESILIENT_PAGE_REBUILDING = 2,
    RESILIENT_PAGE_FAILED = 3,
} resilient_page_state_t;

typedef enum {
    RESILIENT_PAGE_OK = 0,
    RESILIENT_PAGE_RESULT_DEGRADED = 1,
    RESILIENT_PAGE_RESULT_REBUILT = 2,
    RESILIENT_PAGE_ERROR_ARGUMENT = -1,
    RESILIENT_PAGE_ERROR_STALE = -2,
    RESILIENT_PAGE_ERROR_CAPACITY = -3,
    RESILIENT_PAGE_ERROR_CORRUPT = -4,
    RESILIENT_PAGE_ERROR_FAILED = -5,
    RESILIENT_PAGE_ERROR_BUSY = -6,
    RESILIENT_PAGE_ERROR_INJECTED = -7,
} resilient_page_result_t;

typedef struct {
    uint32_t slot;
    uint32_t generation;
} resilient_page_handle_t;

void resilient_page_initialize(void);
resilient_page_result_t resilient_page_create(
    const void *initial_bytes, size_t length,
    resilient_page_handle_t *handle_out);
resilient_page_result_t resilient_page_destroy(resilient_page_handle_t handle);
resilient_page_result_t resilient_page_read(
    resilient_page_handle_t handle, void *bytes_out, size_t capacity,
    resilient_page_state_t *state_out);
resilient_page_result_t resilient_page_write(
    resilient_page_handle_t handle, size_t offset,
    const void *bytes, size_t length);
resilient_page_result_t resilient_page_scrub(
    resilient_page_handle_t handle, resilient_page_state_t *state_out);
resilient_page_result_t resilient_page_fail_domain(
    resilient_page_domain_t domain);
resilient_page_result_t resilient_page_rebuild(
    resilient_page_handle_t handle);
resilient_page_result_t resilient_page_get_state(
    resilient_page_handle_t handle, resilient_page_state_t *state_out);

#ifdef REIST_HOST_TEST
typedef enum {
    RESILIENT_PAGE_TEST_FAULT_NONE = 0,
    RESILIENT_PAGE_TEST_FAULT_WRITE_BEFORE_PREPARE = 1,
    RESILIENT_PAGE_TEST_FAULT_WRITE_AFTER_FIRST_PREPARE = 2,
    RESILIENT_PAGE_TEST_FAULT_WRITE_BEFORE_COMMIT = 3,
    RESILIENT_PAGE_TEST_FAULT_WRITE_AFTER_COMMIT = 4,
    RESILIENT_PAGE_TEST_FAULT_SCRUB = 5,
    RESILIENT_PAGE_TEST_FAULT_REBUILD_AFTER_COPY = 6,
} resilient_page_test_fault_stage_t;

void resilient_page_test_arm_domain_failure(
    resilient_page_test_fault_stage_t stage,
    resilient_page_domain_t domain);
resilient_page_result_t resilient_page_test_corrupt_replica(
    resilient_page_handle_t handle, uint32_t replica_index,
    size_t offset, uint8_t xor_mask, uint32_t reseal_crc);
#endif

#endif
