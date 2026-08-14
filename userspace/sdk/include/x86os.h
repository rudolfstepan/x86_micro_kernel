#ifndef X86OS_USER_API_H
#define X86OS_USER_API_H

#include <stddef.h>
#include <stdint.h>

enum {
    X86OS_SYS_PUTCHAR = 0,
    X86OS_SYS_PRINT_NUMBER = 1,
    X86OS_SYS_DELAY = 2,
    X86OS_SYS_WAIT_ENTER = 3,
    X86OS_SYS_MALLOC = 4,
    X86OS_SYS_FREE = 5,
    X86OS_SYS_REALLOC = 6,
    X86OS_SYS_GETCHAR = 7,
    X86OS_SYS_EXIT = 9,
    X86OS_SYS_GET_DATE = 10,
    X86OS_SYS_GET_TIME = 11,
    X86OS_SYS_UPTIME_MS = 12,
    X86OS_SYS_MEMORY_KB = 13,
    X86OS_SYS_OPEN = 14,
    X86OS_SYS_READ = 15,
    X86OS_SYS_CLOSE = 16,
    X86OS_SYS_STAT = 17,
    X86OS_SYS_READDIR = 18,
    X86OS_SYS_CREATE = 19,
    X86OS_SYS_WRITE = 20,
    X86OS_SYS_UNLINK = 21,
    X86OS_SYS_GETPID = 22,
    X86OS_SYS_SPAWN = 23,
    X86OS_SYS_WAIT = 24,
    X86OS_SYS_READDIR_BATCH = 25,
    X86OS_SYS_PROCESS_INFO = 26,
    X86OS_SYS_KILL = 27,
    X86OS_SYS_GETCWD = 28,
    X86OS_SYS_CHDIR = 29,
    X86OS_SYS_SPAWNV = 30,
    X86OS_SYS_DRIVE_INFO = 31,
    X86OS_SYS_SPACE = 32,
    X86OS_SYS_MKDIR = 33,
    X86OS_SYS_RMDIR = 34,
    X86OS_SYS_CLEAR = 35,
    X86OS_SYS_SET_CURSOR = 36,
    X86OS_SYS_TERMINAL_WRITE = 37,
    X86OS_SYS_TERMINAL_DRAW = 38,
    X86OS_SYS_GETCHAR_NONBLOCKING = 39,
    X86OS_SYS_YIELD = 40,
    X86OS_SYS_SLEEP_MS = 41,
    X86OS_SYS_MONOTONIC_MS = 42,
    X86OS_SYS_MEMORY_STATS = 43,
    X86OS_SYS_DISPLAY_INFO = 44,
    X86OS_SYS_FILL_RECT = 45,
    X86OS_SYS_DRAW_TEXT = 46,
    X86OS_SYS_RENAME = 47,
    X86OS_SYS_FSYNC = 48,
    X86OS_SYS_IPC_CREATE = 49,
    X86OS_SYS_IPC_SEND = 50,
    X86OS_SYS_IPC_RECEIVE = 51,
    X86OS_SYS_IPC_CLOSE = 52,
    X86OS_SYS_IPC_SEND_TIMEOUT = 53,
    X86OS_SYS_IPC_RECEIVE_TIMEOUT = 54,
    X86OS_SYS_IPC_DELEGATE = 55,
    X86OS_SYS_REIST_REPORT = 56,
    X86OS_SYS_SERVICE_CONNECT = 57,
    X86OS_SYS_IPC_RELEASE = 58,
    X86OS_SYS_NETWORK_PROBE = 59,
    X86OS_SYS_NETWORK_PROBE_ID = 60,
    X86OS_SYS_NETWORK_PROBE_STATS = 61
};

enum {
    X86OS_FILE = 1,
    X86OS_DIRECTORY = 2
};

typedef struct {
    char name[256];
    uint32_t type;
    uint32_t size;
} x86os_file_info_t;

enum {
    X86OS_PROCESS_READY = 0,
    X86OS_PROCESS_RUNNING = 1,
    X86OS_PROCESS_SLEEPING = 2,
    X86OS_PROCESS_WAITING = 3,
    X86OS_PROCESS_ZOMBIE = 4
};

typedef struct {
    int32_t pid;
    int32_t parent_pid;
    int32_t state;
    int32_t exit_status;
    char name[32];
} x86os_process_info_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint64_t detected_usable_bytes;
    uint64_t managed_bytes;
    uint64_t reserved_bytes;
    uint64_t allocated_frame_bytes;
    uint64_t free_frame_bytes;
    uint64_t heap_capacity_bytes;
    uint64_t heap_used_bytes;
    uint64_t heap_free_bytes;
    uint64_t heap_largest_free_block;
    uint64_t heap_arena_count;
} x86os_memory_stats_t;

#define X86OS_MEMORY_STATS_VERSION 1U

#define X86OS_IPC_MAX_MESSAGE_SIZE 128U
#define X86OS_IPC_QUEUE_DEPTH 4U
#define X86OS_IPC_MAX_CAPABILITIES_PER_PROCESS 8U
#define X86OS_IPC_MESSAGE_VERSION 1U
#define X86OS_IPC_INVALID_HANDLE 0U
#define X86OS_IPC_DEFAULT_TIMEOUT_MS 1000U
#define X86OS_IPC_RIGHT_SEND 0x01U
#define X86OS_IPC_RIGHT_RECEIVE 0x02U
#define X86OS_IPC_RIGHT_CONTROL 0x04U

#define X86OS_REIST_REPORT_SELF_TEST 1U
#define X86OS_REIST_REPORT_PROGRESS 2U
#define X86OS_REIST_REPORT_INVALID 3U
#define X86OS_REIST_REPORT_NETWORK_HEADER 4U
#define X86OS_REIST_REPORT_NETWORK_PROBE_ID 5U
#define X86OS_REIST_REPORT_NETWORK_DEGRADED 6U
#define X86OS_REIST_NETWORK_DEGRADED_SEMANTIC 3U
#define X86OS_SERVICE_DIAGNOSTIC 1U

#define X86OS_NETWORK_PROBE_STATS_VERSION 1U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t expired;
    uint32_t queue_fallback;
    uint32_t semantic_reject;
    uint32_t reserved;
} x86os_network_probe_stats_t;

typedef uint32_t x86os_ipc_handle_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t length;
    uint8_t payload[X86OS_IPC_MAX_MESSAGE_SIZE];
} x86os_ipc_message_t;

#define X86OS_DISPLAY_ABI_VERSION 1U
#define X86OS_DISPLAY_MAX_TEXT 256U

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bits_per_pixel;
    uint32_t red_field_position;
    uint32_t red_mask_size;
    uint32_t green_field_position;
    uint32_t green_mask_size;
    uint32_t blue_field_position;
    uint32_t blue_mask_size;
    uint32_t font_width;
    uint32_t font_height;
} x86os_display_info_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t rgb;
} x86os_display_rect_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    int32_t x;
    int32_t y;
    uint32_t foreground_rgb;
    uint32_t background_rgb;
    uint32_t text_address;
    uint32_t text_length;
} x86os_display_text_t;

enum {
    X86OS_DRIVE_ATA = 1,
    X86OS_DRIVE_FDD = 2
};

typedef struct {
    uint32_t type;
    char name[8];
    char mount_point[64];
} x86os_drive_info_t;

typedef struct {
    uint64_t total_bytes;
    uint64_t free_bytes;
} x86os_space_info_t;

#define X86OS_READDIR_BATCH_CAPACITY 4U

uintptr_t x86os_syscall(uint32_t number, uintptr_t argument1,
                        uintptr_t argument2, uintptr_t argument3);
void x86os_putchar(char value);
void x86os_puts(const char* text);
void x86os_print_number(int value);
void x86os_delay(uint32_t milliseconds);
int x86os_sleep_ms(uint32_t milliseconds);
int x86os_yield(void);
int x86os_monotonic_ms(uint64_t* value);
int x86os_memory_stats(x86os_memory_stats_t* stats);
int x86os_ipc_create(x86os_ipc_handle_t* handle);
int x86os_ipc_send(x86os_ipc_handle_t handle,
                   const x86os_ipc_message_t* message);
int x86os_ipc_receive(x86os_ipc_handle_t handle,
                      x86os_ipc_message_t* message);
int x86os_ipc_send_timeout(x86os_ipc_handle_t handle,
                           const x86os_ipc_message_t* message,
                           uint32_t timeout_ms);
int x86os_ipc_receive_timeout(x86os_ipc_handle_t handle,
                              x86os_ipc_message_t* message,
                              uint32_t timeout_ms);
int x86os_ipc_close(x86os_ipc_handle_t handle);
int x86os_ipc_release(x86os_ipc_handle_t handle);
int x86os_network_probe(void);
int x86os_network_probe_id(uint32_t *probe_id);
int x86os_network_probe_stats(x86os_network_probe_stats_t *stats);
int x86os_ipc_delegate(x86os_ipc_handle_t handle, int target_pid,
                       uint32_t rights);
int x86os_reist_report(uint32_t report_type, uint32_t value);
int x86os_service_connect(uint32_t service_id,
                          x86os_ipc_handle_t* handle);
int x86os_display_info(x86os_display_info_t* info);
int x86os_fill_rect(int32_t x, int32_t y, uint32_t width, uint32_t height,
                    uint32_t rgb);
int x86os_draw_text_pixels(int32_t x, int32_t y, const char* text,
                           size_t length, uint32_t foreground_rgb,
                           uint32_t background_rgb);
int x86os_getchar(void);
int x86os_getchar_nonblocking(void);
void* x86os_malloc(size_t size);
void x86os_free(void* pointer);
void* x86os_realloc(void* pointer, size_t size);
uint32_t x86os_get_date(void);
uint32_t x86os_get_time(void);
uint32_t x86os_uptime_ms(void);
uint32_t x86os_memory_kb(void);
int x86os_open(const char* path);
int x86os_read(int descriptor, void* buffer, size_t size);
int x86os_close(int descriptor);
int x86os_stat(const char* path, x86os_file_info_t* info);
int x86os_readdir(const char* path, uint32_t index, x86os_file_info_t* info);
int x86os_readdir_batch(const char* path, uint32_t index,
                        x86os_file_info_t* entries);
int x86os_create(const char* path);
int x86os_write(int descriptor, const void* buffer, size_t size);
int x86os_fsync(int descriptor);
int x86os_unlink(const char* path);
int x86os_rename(const char* old_path, const char* new_path);
int x86os_getpid(void);
int x86os_spawn(const char* path);
int x86os_spawnv(const char* path, int argc, const char* const* argv);
int x86os_wait(int pid, int* status);
int x86os_process_info(uint32_t index, x86os_process_info_t* info);
int x86os_kill(int pid);
int x86os_getcwd(char* buffer, size_t size);
int x86os_chdir(const char* path);
int x86os_drive_info(uint32_t index, x86os_drive_info_t* info);
int x86os_space(const char* path, x86os_space_info_t* info);
int x86os_mkdir(const char* path);
int x86os_rmdir(const char* path);
void x86os_clear(void);
void x86os_set_cursor(unsigned int column, unsigned int row);
void x86os_draw_text(unsigned int column, unsigned int row,
                     const char* text, size_t length);
void x86os_exit(int status) __attribute__((noreturn));

#endif
