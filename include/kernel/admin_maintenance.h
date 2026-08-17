#ifndef KERNEL_ADMIN_MAINTENANCE_H
#define KERNEL_ADMIN_MAINTENANCE_H

#include <stdbool.h>
#include <stdint.h>

#define ADMIN_STORAGE_SYSCALL 90U
#define SYS_ADMIN_STORAGE ADMIN_STORAGE_SYSCALL
#define ADMIN_MAINTENANCE_ABI_VERSION 1U
#define ADMIN_MAINTENANCE_DRAIN_DEFAULT_MS 500U
#define ADMIN_MAINTENANCE_DRAIN_MAX_MS 2000U
#define ADMIN_MAINTENANCE_PATH_MAX 64U
#define ADMIN_MAINTENANCE_FS_MAX 16U

#define ADMIN_EROOT (-1001)
#define ADMIN_ESTATE (-1002)

typedef enum {
    ADMIN_STORAGE_STATUS = 0,
    ADMIN_STORAGE_DEVICE_DOWN = 1,
    ADMIN_STORAGE_DEVICE_UP = 2,
    ADMIN_STORAGE_MOUNT = 3,
    ADMIN_STORAGE_UMOUNT = 4,
} admin_storage_command_t;

typedef enum {
    ADMIN_RESOURCE_ONLINE = 1,
    ADMIN_RESOURCE_TRANSITION = 2,
    ADMIN_RESOURCE_DOWN = 3,
    ADMIN_RESOURCE_FAILED = 4,
} admin_resource_state_t;

#define ADMIN_RESOURCE_MOUNTED   (1U << 0)
#define ADMIN_RESOURCE_ROOT      (1U << 1)
#define ADMIN_RESOURCE_PARENT    (1U << 2)
#define ADMIN_RESOURCE_BLOCKED   (1U << 3)
#define ADMIN_RESOURCE_AVAILABLE (1U << 4)
#define ADMIN_RESOURCE_READ_ONLY (1U << 5)

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t command;
    uint32_t resource;
    uint32_t drain_timeout_ms;
    uint32_t reserved;
    char fs_type[ADMIN_MAINTENANCE_FS_MAX];
    char mount_path[ADMIN_MAINTENANCE_PATH_MAX];
} admin_storage_request_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t resource;
    uint32_t state;
    uint32_t generation;
    uint32_t flags;
    uint32_t parent_resource;
    uint32_t open_handles;
    uint32_t drive_type;
    uint32_t reserved;
    char name[8];
    char fs_type[ADMIN_MAINTENANCE_FS_MAX];
    char mount_path[ADMIN_MAINTENANCE_PATH_MAX];
} admin_storage_result_t;

bool admin_maintenance_init(void);
int admin_maintenance_execute(int pid, uint32_t process_generation,
                              const admin_storage_request_t *request,
                              admin_storage_result_t *result,
                              uint64_t now_ms);
void admin_maintenance_process_cleanup(int pid,
                                       uint32_t process_generation);

#endif
