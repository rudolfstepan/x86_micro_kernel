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
    static uint8_t bulk_source[STORAGE_REQUEST_BULK_MAX_BYTES];
    static uint8_t bulk_target[STORAGE_REQUEST_BULK_MAX_BYTES];
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
        descriptor.version != STORAGE_REQUEST_VERSION ||
        descriptor.struct_size != sizeof(descriptor) ||
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

    storage_request_descriptor_v2_t descriptor_v2;
    if (storage_request_submit(3, 5U, &write, write_data, 15U,
                               &write_handle) != 0 ||
        storage_request_claim_v2(8, 12U, 16U, &descriptor_v2,
                                 transfer) != -13 ||
        storage_request_claim_v2(7, 11U, 16U, &descriptor_v2,
                                 transfer) != 0 ||
        descriptor_v2.version != STORAGE_REQUEST_DESCRIPTOR_V2_VERSION ||
        descriptor_v2.struct_size != sizeof(descriptor_v2) ||
        descriptor_v2.handle != write_handle ||
        descriptor_v2.client_pid != 3 ||
        descriptor_v2.client_generation != 5U ||
        descriptor_v2.service_generation != 11U ||
        memcmp(write_data, transfer, sizeof(write_data)) != 0 ||
        storage_request_complete(7, 11U, write_handle, 0, 0) != 0 ||
        storage_request_collect(3, 5U, write_handle, &result, 0) != 0)
        return 27;

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

    storage_request_handle_t queued_cancel = 0U;
    if (storage_request_submit(3, 5U, &read, 0, 42U,
                               &queued_cancel) != 0 ||
        storage_request_cancel(4, 5U, queued_cancel) != -13 ||
        storage_request_cancel(3, 5U, queued_cancel) != 0 ||
        storage_request_collect(3, 5U, queued_cancel, &result,
                                transfer) != -22)
        return 24;

    storage_request_handle_t claimed_cancel = 0U;
    if (storage_request_submit(3, 5U, &read, 0, 43U,
                               &claimed_cancel) != 0 ||
        storage_request_claim(7, 11U, 44U, &descriptor, 0) != 0 ||
        descriptor.handle != claimed_cancel ||
        storage_request_cancel(3, 5U, claimed_cancel) != 0 ||
        storage_request_cancel(3, 5U, claimed_cancel) != 0 ||
        storage_request_collect(3, 5U, claimed_cancel, &result,
                                transfer) != -125 ||
        storage_request_stats(&stats) != 0 || stats.active_requests != 1U ||
        storage_request_complete(7, 11U, claimed_cancel, 0, 0) != 0 ||
        storage_request_stats(&stats) != 0 || stats.active_requests != 0U ||
        storage_request_collect(3, 5U, claimed_cancel, &result,
                                transfer) != -22)
        return 25;

    storage_request_handle_t completed_cancel = 0U;
    if (storage_request_submit(3, 5U, &read, 0, 45U,
                               &completed_cancel) != 0 ||
        storage_request_claim(7, 11U, 46U, &descriptor, 0) != 0 ||
        storage_request_complete(7, 11U, completed_cancel, -5, 0) != 0 ||
        storage_request_cancel(3, 6U, completed_cancel) != -13 ||
        storage_request_cancel(3, 5U, completed_cancel) != 0 ||
        storage_request_collect(3, 5U, completed_cancel, &result,
                                transfer) != -22)
        return 26;

    storage_request_submit_t shadow_stat = {
        STORAGE_REQUEST_VERSION, sizeof(shadow_stat),
        STORAGE_REQUEST_VFS_SHADOW_STAT, 0U, 0U,
        STORAGE_REQUEST_BLOCK_SIZE, 1000U,
    };
    fill(write_data, 17U);
    storage_request_handle_t shadow_handle = 0U;
    if (storage_request_submit(3, 5U, &shadow_stat, write_data, 47U,
                               &shadow_handle) != 0 ||
        storage_request_claim(7, 11U, 48U, &descriptor, transfer) != 0 ||
        descriptor.operation != STORAGE_REQUEST_VFS_SHADOW_STAT ||
        memcmp(write_data, transfer, sizeof(write_data)) != 0) return 22;
    fill(read_data, 71U);
    if (storage_request_complete(
            7, 11U, shadow_handle, 0, read_data) != 0 ||
        storage_request_collect(
            3, 5U, shadow_handle, &result, transfer) != 0 || result != 0 ||
        memcmp(read_data, transfer, sizeof(read_data)) != 0) return 23;

    storage_request_submit_t bulk_read = {
        STORAGE_REQUEST_VERSION, sizeof(bulk_read),
        STORAGE_REQUEST_VFS_BULK_READ, 0U, 0U,
        STORAGE_REQUEST_BLOCK_SIZE, 1000U,
    };
    fill(write_data, 29U);
    for (uint32_t index = 0U; index < sizeof(bulk_source); ++index)
        bulk_source[index] = (uint8_t)(index * 17U + 3U);
    storage_request_handle_t bulk_handle = 0U;
    uint32_t bulk_amount = 0U;
    if (storage_request_submit(3, 5U, &bulk_read, write_data, 49U,
                               &bulk_handle) != 0 ||
        storage_request_claim_v2(7, 11U, 50U, &descriptor_v2,
                                 transfer) != 0 ||
        descriptor_v2.operation != STORAGE_REQUEST_VFS_BULK_READ ||
        storage_request_bulk_publish(8, 12U, bulk_handle, bulk_source,
                                     sizeof(bulk_source)) != -13 ||
        storage_request_bulk_publish(7, 11U, bulk_handle, bulk_source,
                                     sizeof(bulk_source)) != 0 ||
        storage_request_complete(7, 11U, bulk_handle, 0, read_data) != 0 ||
        storage_request_collect(3, 5U, bulk_handle, &result, transfer) != -22 ||
        storage_request_bulk_collect(4, 5U, bulk_handle, &result, transfer,
                                     bulk_target, sizeof(bulk_target),
                                     &bulk_amount) != -13 ||
        storage_request_bulk_collect(3, 5U, bulk_handle, &result, transfer,
                                     bulk_target, sizeof(bulk_target),
                                     &bulk_amount) != 0 ||
        result != 0 || bulk_amount != sizeof(bulk_source) ||
        memcmp(bulk_source, bulk_target, sizeof(bulk_source)) != 0 ||
        memcmp(read_data, transfer, sizeof(read_data)) != 0) return 28;

    if (storage_request_submit(3, 5U, &bulk_read, write_data, 51U,
                               &bulk_handle) != 0 ||
        storage_request_claim(7, 11U, 52U, &descriptor, transfer) != 0 ||
        storage_request_bulk_publish(7, 11U, bulk_handle, bulk_source,
                                     sizeof(bulk_source)) != 0 ||
        storage_request_test_corrupt_bulk(bulk_handle) != 0 ||
        storage_request_complete(7, 11U, bulk_handle, 0, read_data) != 0 ||
        storage_request_bulk_collect(3, 5U, bulk_handle, &result, transfer,
                                     bulk_target, sizeof(bulk_target),
                                     &bulk_amount) != -84) return 29;

    storage_request_handle_t bulk_slots[STORAGE_REQUEST_BULK_CAPACITY];
    storage_request_handle_t bulk_excess = 0U;
    for (uint32_t index = 0U; index < STORAGE_REQUEST_BULK_CAPACITY; ++index)
        if (storage_request_submit(20 + (int)index, 5U, &bulk_read,
                                   write_data, 53U, &bulk_slots[index]) != 0)
            return 30;
    if (storage_request_submit(30, 5U, &bulk_read, write_data, 53U,
                               &bulk_excess) != -28) return 31;
    for (uint32_t index = 0U; index < STORAGE_REQUEST_BULK_CAPACITY; ++index)
        storage_request_cancel_process(20 + (int)index, 5U);

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
