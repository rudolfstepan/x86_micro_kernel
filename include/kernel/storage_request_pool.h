/**
 * @file include/kernel/storage_request_pool.h
 * @brief Fester Request-Pool für begrenzte Storage-Operationen.
 *
 * Layer: Ring-0 public subsystem interface.
 * Contract: Slots folgen definierten Zuständen und bleiben generationsgebunden.
 * Safety: Keine Heap-Allokation; Erschöpfung und stale Completion werden abgewiesen.
 */
#ifndef KERNEL_STORAGE_REQUEST_POOL_H
#define KERNEL_STORAGE_REQUEST_POOL_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define STORAGE_REQUEST_VERSION 1U
#define STORAGE_REQUEST_POOL_CAPACITY 8U
#define STORAGE_REQUEST_BLOCK_SIZE 512U
#define STORAGE_REQUEST_MAX_PER_CLIENT 2U
#define STORAGE_REQUEST_MAX_TIMEOUT_MS 60000U
#define STORAGE_REQUEST_INVALID_HANDLE 0U
#define STORAGE_REQUEST_STATS_VERSION 1U

typedef uint32_t storage_request_handle_t;

typedef enum {
    STORAGE_REQUEST_BLOCK_READ = 1,
    STORAGE_REQUEST_BLOCK_WRITE = 2,
    STORAGE_REQUEST_BLOCK_FLUSH = 3,
    STORAGE_REQUEST_VFS_READ = 4,
    STORAGE_REQUEST_VFS_WRITE = 5,
    STORAGE_REQUEST_VFS_SYNC = 6,
    STORAGE_REQUEST_FORMAT_FAT12 = 7,
    STORAGE_REQUEST_FORMAT_FAT32 = 8,
    STORAGE_REQUEST_FORMAT_FAT32_SCAN = 9,
    STORAGE_REQUEST_FORMAT_FAT32_PREPARE = 10,
    STORAGE_REQUEST_CHECK_FAT12 = 11,
    STORAGE_REQUEST_REPAIR_FAT12_MIRROR = 12,
    STORAGE_REQUEST_REPAIR_FAT12_CHAINS = 13,
    STORAGE_REQUEST_REPAIR_FAT12_SHORT_FILES = 14,
    STORAGE_REQUEST_RECLAIM_FAT12_ORPHANS = 15,
    STORAGE_REQUEST_REPAIR_FAT12_LOOPS = 16,
    STORAGE_REQUEST_REPAIR_FAT12_DIRECTORY_LOOPS = 17,
    STORAGE_REQUEST_REPAIR_FAT12_SHORT_LOOPS = 18,
    STORAGE_REQUEST_REPAIR_FAT12_CROSSLINKS = 19,
    STORAGE_REQUEST_REPAIR_FAT12_DIRECTORY_SIZE = 20,
    STORAGE_REQUEST_REPAIR_FAT12_VOLUME_LABEL = 21,
} storage_request_operation_t;

#define STORAGE_REQUEST_READ STORAGE_REQUEST_BLOCK_READ
#define STORAGE_REQUEST_WRITE STORAGE_REQUEST_BLOCK_WRITE
#define STORAGE_REQUEST_FLUSH STORAGE_REQUEST_BLOCK_FLUSH

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    uint32_t resource;
    uint32_t offset;
    uint32_t length;
    uint32_t timeout_ms;
} storage_request_submit_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    storage_request_handle_t handle;
    uint32_t operation;
    uint32_t resource;
    uint32_t offset;
    uint32_t length;
} storage_request_descriptor_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t active_requests;
    uint32_t request_high_water;
    uint32_t client_capacity_rejections;
    uint32_t pool_capacity_rejections;
} storage_request_stats_t;

int storage_request_pool_init(void);
int storage_request_bind_service(int pid, uint32_t generation);
void storage_request_unbind_service(int pid, uint32_t generation);
int storage_request_submit(int client_pid, uint32_t client_generation,
                           const storage_request_submit_t *request,
                           const uint8_t *block_data, uint64_t now_ms,
                           storage_request_handle_t *handle_out);
int storage_request_claim(int service_pid, uint32_t service_generation,
                          uint64_t now_ms,
                          storage_request_descriptor_t *request_out,
                          uint8_t *block_data_out);
int storage_request_complete(int service_pid, uint32_t service_generation,
                             storage_request_handle_t handle, int32_t result,
                             const uint8_t *block_data);
int storage_request_collect(int client_pid, uint32_t client_generation,
                            storage_request_handle_t handle,
                            int32_t *result_out, uint8_t *block_data_out);
int storage_request_collect_ex(int client_pid, uint32_t client_generation,
                               storage_request_handle_t handle,
                               int32_t *result_out, uint8_t *block_data_out,
                               uint32_t *data_length_out);
void storage_request_cancel_process(int pid, uint32_t generation);
int storage_request_stats(storage_request_stats_t *stats_out);

#ifdef REIST_HOST_TEST
int storage_request_test_corrupt_data(storage_request_handle_t handle,
                                      bool corrupt_both_copies);
int storage_request_test_corrupt_metadata(storage_request_handle_t handle,
                                          bool corrupt_both_copies);
#endif

#endif
