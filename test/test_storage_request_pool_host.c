/**
 * @file test/test_storage_request_pool_host.c
 * @brief Hostseitiger Regressionstest für storage request pool.
 *
 * Layer: Host test harness.
 * Contract: Prüft beobachtbares Verhalten und feste Fehlergrenzen ohne Zielhardware.
 * Safety: Testdoubles dürfen Produktionsverträge nicht abschwächen oder Erfolg vortäuschen.
 */
#include <stdint.h>
#include <string.h>

#include "include/kernel/storage_request_pool.h"

static void fill(uint8_t block[STORAGE_REQUEST_BLOCK_SIZE], uint8_t seed) {
    for (uint32_t index = 0U; index < STORAGE_REQUEST_BLOCK_SIZE; ++index)
        block[index] = (uint8_t)(seed + index);
}

int main(void) {
    if (storage_request_pool_init() != 0) return 100;
    storage_request_stats_t stats;
    if (storage_request_stats(0) != -22 ||
        storage_request_stats(&stats) != 0 ||
        stats.version != STORAGE_REQUEST_STATS_VERSION ||
        stats.struct_size != sizeof(stats) || stats.active_requests != 0U ||
        stats.request_high_water != 0U) return 101;
    if (storage_request_bind_service(7, 11U) != 0 ||
        storage_request_bind_service(8, 12U) != -13) return 1;

    uint8_t write_data[STORAGE_REQUEST_BLOCK_SIZE];
    uint8_t transfer[STORAGE_REQUEST_BLOCK_SIZE];
    fill(write_data, 3U);
    storage_request_submit_t write = {
        STORAGE_REQUEST_VERSION, sizeof(write), STORAGE_REQUEST_WRITE,
        1U, 42U, STORAGE_REQUEST_BLOCK_SIZE, 1000U,
    };
    storage_request_handle_t write_handle = 0U;
    if (storage_request_submit(3, 5U, &write, write_data, 10U,
                               &write_handle) != 0 || write_handle == 0U)
        return 2;
    storage_request_descriptor_t descriptor;
    if (storage_request_claim(8, 12U, 11U, &descriptor, transfer) != -13 ||
        storage_request_claim(7, 11U, 11U, &descriptor, transfer) != 0 ||
        descriptor.handle != write_handle || descriptor.resource != 1U ||
        descriptor.offset != 42U ||
        memcmp(write_data, transfer, sizeof(write_data)) != 0) return 3;
    if (storage_request_complete(8, 12U, write_handle, 0, 0) != -13 ||
        storage_request_complete(7, 11U, write_handle, 0, 0) != 0) return 4;
    int32_t result = -1;
    if (storage_request_collect(4, 5U, write_handle, &result, 0) != -13 ||
        storage_request_collect(3, 5U, write_handle, &result, 0) != 0 ||
        result != 0 ||
        storage_request_collect(3, 5U, write_handle, &result, 0) >= 0)
        return 5;

    if (storage_request_submit(3, 5U, &write, write_data, 20U,
                               &write_handle) != 0 ||
        storage_request_test_corrupt_data(write_handle, false) != 0 ||
        storage_request_claim(7, 11U, 21U, &descriptor, transfer) != 0 ||
        memcmp(write_data, transfer, sizeof(write_data)) != 0 ||
        storage_request_complete(7, 11U, write_handle, 0, 0) != 0 ||
        storage_request_collect(3, 5U, write_handle, &result, 0) != 0)
        return 13;
    if (storage_request_submit(3, 5U, &write, write_data, 30U,
                               &write_handle) != 0 ||
        storage_request_test_corrupt_data(write_handle, true) != 0 ||
        storage_request_claim(7, 11U, 31U, &descriptor, transfer) != -84)
        return 14;
    storage_request_cancel_process(3, 5U);

    storage_request_submit_t read = {
        STORAGE_REQUEST_VERSION, sizeof(read), STORAGE_REQUEST_READ,
        1U, 43U, STORAGE_REQUEST_BLOCK_SIZE, 1000U,
    };
    storage_request_handle_t read_handle = 0U;
    if (storage_request_submit(3, 5U, &read, 0, 40U, &read_handle) != 0 ||
        storage_request_claim(7, 11U, 41U, &descriptor, 0) != 0) return 6;
    uint8_t read_data[STORAGE_REQUEST_BLOCK_SIZE];
    fill(read_data, 99U);
    if (storage_request_complete(7, 11U, read_handle, 0, read_data) != 0 ||
        storage_request_collect(3, 5U, read_handle, &result, transfer) != 0 ||
        memcmp(read_data, transfer, sizeof(read_data)) != 0) return 7;

    if (storage_request_submit(3, 5U, &read, 0, 100U, &read_handle) != 0 ||
        storage_request_claim(7, 11U, 1100U, &descriptor, 0) != -11 ||
        storage_request_collect(3, 5U, read_handle, &result, transfer) != 0 ||
        result != -110) return 15;

    storage_request_handle_t quota[STORAGE_REQUEST_MAX_PER_CLIENT];
    for (uint32_t index = 0U; index < STORAGE_REQUEST_MAX_PER_CLIENT; ++index)
        if (storage_request_submit(3, 5U, &read, 0, 50U,
                                   &quota[index]) != 0) return 8;
    storage_request_handle_t excess;
    if (storage_request_submit(3, 5U, &read, 0, 50U, &excess) != -28)
        return 9;
    if (storage_request_stats(&stats) != 0 ||
        stats.active_requests != STORAGE_REQUEST_MAX_PER_CLIENT ||
        stats.request_high_water < STORAGE_REQUEST_MAX_PER_CLIENT ||
        stats.client_capacity_rejections == 0U) return 16;
    storage_request_cancel_process(3, 5U);
    if (storage_request_stats(&stats) != 0 || stats.active_requests != 0U)
        return 17;

    storage_request_handle_t handles[STORAGE_REQUEST_POOL_CAPACITY];
    for (uint32_t index = 0U; index < STORAGE_REQUEST_POOL_CAPACITY; ++index)
        if (storage_request_submit(10 + (int)index, 5U, &read, 0, 60U,
                                   &handles[index]) != 0)
            return 8;
    if (storage_request_submit(30, 5U, &read, 0, 60U, &excess) != -28)
        return 9;
    if (storage_request_stats(&stats) != 0 ||
        stats.active_requests != STORAGE_REQUEST_POOL_CAPACITY ||
        stats.request_high_water != STORAGE_REQUEST_POOL_CAPACITY ||
        stats.pool_capacity_rejections == 0U) return 18;
    for (uint32_t index = 0U; index < STORAGE_REQUEST_POOL_CAPACITY; ++index)
        storage_request_cancel_process(10 + (int)index, 5U);
    if (storage_request_stats(&stats) != 0 || stats.active_requests != 0U ||
        stats.request_high_water != STORAGE_REQUEST_POOL_CAPACITY) return 19;
    if (storage_request_claim(7, 11U, 61U, &descriptor, 0) != -11) return 10;
    for (uint32_t index = 0U; index < STORAGE_REQUEST_POOL_CAPACITY; ++index)
        if (storage_request_collect(3, 5U, handles[index], &result,
                                    transfer) >= 0) return 11;

    storage_request_unbind_service(7, 11U);
    if (storage_request_claim(7, 11U, 61U, &descriptor, 0) != -13 ||
        storage_request_bind_service(9, 13U) != 0) return 12;
    storage_request_handle_t stale_handle = 0U;
    if (storage_request_submit(3, 5U, &read, 0, 70U, &stale_handle) != 0 ||
        storage_request_claim(9, 13U, 71U, &descriptor, 0) != 0)
        return 20;
    storage_request_unbind_service(9, 13U);
    if (storage_request_collect(3, 5U, stale_handle, &result, transfer) != -22)
        return 21;
    return 0;
}
