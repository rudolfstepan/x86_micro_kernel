/**
 * @file test/test_resilient_page_host.c
 * @brief Behavioral fault campaign for the bounded resilient-page proof.
 */
#include "include/kernel/resilient_page.h"

#include <stdint.h>
#include <stdio.h>

static uint8_t initial[RESILIENT_PAGE_SIZE];
static uint8_t changed[RESILIENT_PAGE_SIZE];
static uint8_t unrelated_bytes[RESILIENT_PAGE_SIZE];
static uint8_t output[RESILIENT_PAGE_SIZE];

static void fill(uint8_t *bytes, uint8_t seed) {
    for (size_t index = 0; index < RESILIENT_PAGE_SIZE; ++index)
        bytes[index] = (uint8_t)(seed + (uint8_t)(index * 13U));
}

static int same(const uint8_t *left, const uint8_t *right) {
    for (size_t index = 0; index < RESILIENT_PAGE_SIZE; ++index)
        if (left[index] != right[index]) return 0;
    return 1;
}

static int expect_read(resilient_page_handle_t handle,
                       const uint8_t *expected,
                       resilient_page_state_t expected_state) {
    resilient_page_state_t state = RESILIENT_PAGE_FAILED;
    resilient_page_result_t result = resilient_page_read(
        handle, output, sizeof(output), &state);
    if (result < 0 || state != expected_state || !same(output, expected))
        return 0;
    return 1;
}

static int test_capacity_and_stale_handles(void) {
    resilient_page_handle_t handles[RESILIENT_PAGE_CAPACITY];
    resilient_page_initialize();
    fill(initial, 3U);
    for (uint32_t index = 0; index < RESILIENT_PAGE_CAPACITY; ++index)
        if (resilient_page_create(initial, sizeof(initial), &handles[index]) !=
            RESILIENT_PAGE_OK) return 1;
    resilient_page_handle_t extra;
    if (resilient_page_create(initial, sizeof(initial), &extra) !=
        RESILIENT_PAGE_ERROR_CAPACITY) return 2;
    resilient_page_handle_t stale = handles[1];
    if (resilient_page_destroy(stale) != RESILIENT_PAGE_OK) return 3;
    if (resilient_page_create(initial, sizeof(initial), &handles[1]) !=
        RESILIENT_PAGE_OK || handles[1].generation == stale.generation)
        return 4;
    resilient_page_state_t state;
    if (resilient_page_get_state(stale, &state) !=
        RESILIENT_PAGE_ERROR_STALE) return 5;
    return 0;
}

static int test_interrupted_write_and_rebuild(void) {
    resilient_page_handle_t handle;
    resilient_page_handle_t unrelated;
    resilient_page_initialize();
    fill(initial, 7U);
    fill(changed, 41U);
    fill(unrelated_bytes, 113U);
    if (resilient_page_create(initial, sizeof(initial), &handle) !=
        RESILIENT_PAGE_OK) return 10;
    if (resilient_page_create(unrelated_bytes, sizeof(unrelated_bytes),
                              &unrelated) != RESILIENT_PAGE_OK) return 11;

    resilient_page_test_arm_domain_failure(
        RESILIENT_PAGE_TEST_FAULT_WRITE_AFTER_FIRST_PREPARE,
        RESILIENT_PAGE_DOMAIN_A);
    if (resilient_page_write(handle, 0U, changed, sizeof(changed)) !=
        RESILIENT_PAGE_ERROR_INJECTED) return 12;
    if (!expect_read(handle, initial, RESILIENT_PAGE_DEGRADED)) return 13;
    if (!expect_read(unrelated, unrelated_bytes,
                     RESILIENT_PAGE_DEGRADED)) return 14;

    if (resilient_page_write(handle, 0U, changed, sizeof(changed)) !=
        RESILIENT_PAGE_RESULT_DEGRADED) return 15;
    if (!expect_read(handle, changed, RESILIENT_PAGE_DEGRADED)) return 16;
    if (resilient_page_rebuild(handle) != RESILIENT_PAGE_RESULT_REBUILT)
        return 17;
    if (!expect_read(handle, changed, RESILIENT_PAGE_HEALTHY)) return 18;
    return 0;
}

static int test_commit_boundary(void) {
    resilient_page_handle_t handle;
    resilient_page_initialize();
    fill(initial, 11U);
    fill(changed, 83U);
    if (resilient_page_create(initial, sizeof(initial), &handle) !=
        RESILIENT_PAGE_OK) return 20;
    resilient_page_test_arm_domain_failure(
        RESILIENT_PAGE_TEST_FAULT_WRITE_BEFORE_PREPARE,
        RESILIENT_PAGE_DOMAIN_A);
    if (resilient_page_write(handle, 0U, changed, sizeof(changed)) !=
        RESILIENT_PAGE_ERROR_INJECTED) return 21;
    if (!expect_read(handle, initial, RESILIENT_PAGE_DEGRADED)) return 22;

    resilient_page_initialize();
    if (resilient_page_create(initial, sizeof(initial), &handle) !=
        RESILIENT_PAGE_OK) return 23;
    resilient_page_test_arm_domain_failure(
        RESILIENT_PAGE_TEST_FAULT_WRITE_BEFORE_COMMIT,
        RESILIENT_PAGE_DOMAIN_B);
    if (resilient_page_write(handle, 0U, changed, sizeof(changed)) !=
        RESILIENT_PAGE_ERROR_INJECTED) return 24;
    if (!expect_read(handle, initial, RESILIENT_PAGE_DEGRADED)) return 25;

    resilient_page_initialize();
    if (resilient_page_create(initial, sizeof(initial), &handle) !=
        RESILIENT_PAGE_OK) return 26;
    resilient_page_test_arm_domain_failure(
        RESILIENT_PAGE_TEST_FAULT_WRITE_AFTER_COMMIT,
        RESILIENT_PAGE_DOMAIN_A);
    if (resilient_page_write(handle, 0U, changed, sizeof(changed)) !=
        RESILIENT_PAGE_RESULT_DEGRADED) return 27;
    if (!expect_read(handle, changed, RESILIENT_PAGE_DEGRADED)) return 28;
    return 0;
}

static int test_scrub_conflict_and_double_loss(void) {
    resilient_page_handle_t handle;
    resilient_page_state_t state;
    resilient_page_initialize();
    fill(initial, 17U);
    if (resilient_page_create(initial, sizeof(initial), &handle) !=
        RESILIENT_PAGE_OK) return 30;
    resilient_page_test_arm_domain_failure(
        RESILIENT_PAGE_TEST_FAULT_SCRUB, RESILIENT_PAGE_DOMAIN_A);
    if (resilient_page_scrub(handle, &state) !=
            RESILIENT_PAGE_RESULT_DEGRADED ||
        state != RESILIENT_PAGE_DEGRADED) return 31;
    if (!expect_read(handle, initial, RESILIENT_PAGE_DEGRADED)) return 32;

    resilient_page_initialize();
    if (resilient_page_create(initial, sizeof(initial), &handle) !=
        RESILIENT_PAGE_OK) return 33;
    if (resilient_page_test_corrupt_replica(
            handle, 0U, 99U, 0x40U, 0U) != RESILIENT_PAGE_OK) return 34;
    if (resilient_page_scrub(handle, &state) !=
            RESILIENT_PAGE_RESULT_DEGRADED ||
        state != RESILIENT_PAGE_DEGRADED) return 35;
    if (!expect_read(handle, initial, RESILIENT_PAGE_DEGRADED)) return 36;

    resilient_page_initialize();
    if (resilient_page_create(initial, sizeof(initial), &handle) !=
        RESILIENT_PAGE_OK) return 37;
    if (resilient_page_test_corrupt_replica(
            handle, 0U, 101U, 0x01U, 1U) != RESILIENT_PAGE_OK) return 38;
    if (resilient_page_scrub(handle, &state) !=
            RESILIENT_PAGE_ERROR_CORRUPT ||
        state != RESILIENT_PAGE_FAILED) return 39;
    if (resilient_page_read(handle, output, sizeof(output), &state) !=
        RESILIENT_PAGE_ERROR_FAILED) return 40;

    resilient_page_initialize();
    if (resilient_page_create(initial, sizeof(initial), &handle) !=
        RESILIENT_PAGE_OK) return 41;
    if (resilient_page_fail_domain(RESILIENT_PAGE_DOMAIN_A) < 0 ||
        resilient_page_fail_domain(RESILIENT_PAGE_DOMAIN_B) < 0) return 42;
    if (resilient_page_get_state(handle, &state) !=
            RESILIENT_PAGE_ERROR_FAILED ||
        state != RESILIENT_PAGE_FAILED) return 43;
    return 0;
}

static int test_rebuild_interruption_fails_closed(void) {
    resilient_page_handle_t handle;
    resilient_page_state_t state;
    resilient_page_initialize();
    fill(initial, 29U);
    if (resilient_page_create(initial, sizeof(initial), &handle) !=
        RESILIENT_PAGE_OK) return 50;
    if (resilient_page_fail_domain(RESILIENT_PAGE_DOMAIN_A) < 0) return 51;
    resilient_page_test_arm_domain_failure(
        RESILIENT_PAGE_TEST_FAULT_REBUILD_AFTER_COPY,
        RESILIENT_PAGE_DOMAIN_B);
    if (resilient_page_rebuild(handle) != RESILIENT_PAGE_ERROR_FAILED)
        return 52;
    if (resilient_page_get_state(handle, &state) !=
            RESILIENT_PAGE_ERROR_FAILED ||
        state != RESILIENT_PAGE_FAILED) return 53;
    return 0;
}

int main(void) {
    int result = test_capacity_and_stale_handles();
    if (result == 0) result = test_interrupted_write_and_rebuild();
    if (result == 0) result = test_commit_boundary();
    if (result == 0) result = test_scrub_conflict_and_double_loss();
    if (result == 0) result = test_rebuild_interruption_fails_closed();
    if (result != 0) return result;
    puts("RESILIENT_PAGE_HOST_OK");
    return 0;
}
