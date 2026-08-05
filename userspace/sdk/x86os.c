#include "x86os.h"

uintptr_t x86os_syscall(uint32_t number, uintptr_t argument1,
                        uintptr_t argument2, uintptr_t argument3) {
    uintptr_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number), "b"(argument1), "c"(argument2), "d"(argument3)
        : "memory", "cc");
    return result;
}

void x86os_putchar(char value) {
    (void)x86os_syscall(X86OS_SYS_PUTCHAR, (uintptr_t)(uint8_t)value, 0, 0);
}

void x86os_puts(const char* text) {
    if (!text) return;
    size_t length = 0;
    while (text[length] != '\0') ++length;
    if (length != 0)
        (void)x86os_syscall(X86OS_SYS_TERMINAL_WRITE,
                            (uintptr_t)text, length, 0);
}

void x86os_print_number(int value) {
    (void)x86os_syscall(X86OS_SYS_PRINT_NUMBER, (uintptr_t)value, 0, 0);
}

void x86os_delay(uint32_t milliseconds) {
    (void)x86os_syscall(X86OS_SYS_DELAY, milliseconds, 0, 0);
}

int x86os_getchar(void) {
    return (int)x86os_syscall(X86OS_SYS_GETCHAR, 0, 0, 0);
}

int x86os_getchar_nonblocking(void) {
    return (int)x86os_syscall(X86OS_SYS_GETCHAR_NONBLOCKING, 0, 0, 0);
}

void* x86os_malloc(size_t size) {
    uintptr_t result = x86os_syscall(X86OS_SYS_MALLOC, size, 0, 0);
    return (int32_t)result < 0 ? NULL : (void*)result;
}

void x86os_free(void* pointer) {
    (void)x86os_syscall(X86OS_SYS_FREE, (uintptr_t)pointer, 0, 0);
}

void* x86os_realloc(void* pointer, size_t size) {
    uintptr_t result = x86os_syscall(X86OS_SYS_REALLOC,
                                     (uintptr_t)pointer, size, 0);
    return (int32_t)result < 0 ? NULL : (void*)result;
}

uint32_t x86os_get_date(void) {
    return (uint32_t)x86os_syscall(X86OS_SYS_GET_DATE, 0, 0, 0);
}

uint32_t x86os_get_time(void) {
    return (uint32_t)x86os_syscall(X86OS_SYS_GET_TIME, 0, 0, 0);
}

uint32_t x86os_uptime_ms(void) {
    return (uint32_t)x86os_syscall(X86OS_SYS_UPTIME_MS, 0, 0, 0);
}

uint32_t x86os_memory_kb(void) {
    return (uint32_t)x86os_syscall(X86OS_SYS_MEMORY_KB, 0, 0, 0);
}

int x86os_open(const char* path) {
    return (int)x86os_syscall(X86OS_SYS_OPEN, (uintptr_t)path, 0, 0);
}

int x86os_read(int descriptor, void* buffer, size_t size) {
    return (int)x86os_syscall(X86OS_SYS_READ, (uintptr_t)descriptor,
                              (uintptr_t)buffer, size);
}

int x86os_close(int descriptor) {
    return (int)x86os_syscall(X86OS_SYS_CLOSE, (uintptr_t)descriptor, 0, 0);
}

int x86os_stat(const char* path, x86os_file_info_t* info) {
    return (int)x86os_syscall(X86OS_SYS_STAT, (uintptr_t)path,
                              (uintptr_t)info, 0);
}

int x86os_readdir(const char* path, uint32_t index, x86os_file_info_t* info) {
    return (int)x86os_syscall(X86OS_SYS_READDIR, (uintptr_t)path, index,
                              (uintptr_t)info);
}

int x86os_readdir_batch(const char* path, uint32_t index,
                        x86os_file_info_t* entries) {
    return (int)x86os_syscall(X86OS_SYS_READDIR_BATCH, (uintptr_t)path, index,
                              (uintptr_t)entries);
}

int x86os_create(const char* path) {
    return (int)x86os_syscall(X86OS_SYS_CREATE, (uintptr_t)path, 0, 0);
}

int x86os_write(int descriptor, const void* buffer, size_t size) {
    return (int)x86os_syscall(X86OS_SYS_WRITE, (uintptr_t)descriptor,
                              (uintptr_t)buffer, size);
}

int x86os_unlink(const char* path) {
    return (int)x86os_syscall(X86OS_SYS_UNLINK, (uintptr_t)path, 0, 0);
}

int x86os_getpid(void) {
    return (int)x86os_syscall(X86OS_SYS_GETPID, 0, 0, 0);
}

int x86os_spawn(const char* path) {
    return (int)x86os_syscall(X86OS_SYS_SPAWN, (uintptr_t)path, 0, 0);
}

int x86os_spawnv(const char* path, int argc, const char* const* argv) {
    return (int)x86os_syscall(X86OS_SYS_SPAWNV, (uintptr_t)path,
                              (uintptr_t)argv, (uintptr_t)argc);
}

int x86os_wait(int pid, int* status) {
    return (int)x86os_syscall(X86OS_SYS_WAIT, (uintptr_t)pid,
                              (uintptr_t)status, 0);
}

int x86os_process_info(uint32_t index, x86os_process_info_t* info) {
    return (int)x86os_syscall(X86OS_SYS_PROCESS_INFO, index,
                              (uintptr_t)info, 0);
}

int x86os_kill(int pid) {
    return (int)x86os_syscall(X86OS_SYS_KILL, (uintptr_t)pid, 0, 0);
}

int x86os_getcwd(char* buffer, size_t size) {
    return (int)x86os_syscall(X86OS_SYS_GETCWD, (uintptr_t)buffer, size, 0);
}

int x86os_chdir(const char* path) {
    return (int)x86os_syscall(X86OS_SYS_CHDIR, (uintptr_t)path, 0, 0);
}

int x86os_drive_info(uint32_t index, x86os_drive_info_t* info) {
    return (int)x86os_syscall(X86OS_SYS_DRIVE_INFO, index,
                              (uintptr_t)info, 0);
}

int x86os_space(const char* path, x86os_space_info_t* info) {
    return (int)x86os_syscall(X86OS_SYS_SPACE, (uintptr_t)path,
                              (uintptr_t)info, 0);
}

int x86os_mkdir(const char* path) {
    return (int)x86os_syscall(X86OS_SYS_MKDIR, (uintptr_t)path, 0, 0);
}

int x86os_rmdir(const char* path) {
    return (int)x86os_syscall(X86OS_SYS_RMDIR, (uintptr_t)path, 0, 0);
}

void x86os_clear(void) {
    (void)x86os_syscall(X86OS_SYS_CLEAR, 0, 0, 0);
}

void x86os_set_cursor(unsigned int column, unsigned int row) {
    (void)x86os_syscall(X86OS_SYS_SET_CURSOR, column, row, 0);
}

void x86os_draw_text(unsigned int column, unsigned int row,
                     const char* text, size_t length) {
    uintptr_t position = ((uintptr_t)row << 16) | column;
    (void)x86os_syscall(X86OS_SYS_TERMINAL_DRAW, position,
                        (uintptr_t)text, length);
}

void x86os_exit(int status) {
    (void)x86os_syscall(X86OS_SYS_EXIT, (uintptr_t)status, 0, 0);
    for (;;) {
        __asm__ volatile("pause");
    }
}
