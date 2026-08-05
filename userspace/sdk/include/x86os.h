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
    X86OS_SYS_RMDIR = 34
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
int x86os_getchar(void);
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
int x86os_unlink(const char* path);
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
void x86os_exit(int status) __attribute__((noreturn));

#endif
