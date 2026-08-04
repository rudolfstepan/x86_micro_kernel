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
    while (*text != '\0') x86os_putchar(*text++);
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

int x86os_wait(int pid, int* status) {
    int result;
    do {
        result = (int)x86os_syscall(X86OS_SYS_WAIT, (uintptr_t)pid,
                                    (uintptr_t)status, 0);
        if (result == -11) x86os_delay(1);
    } while (result == -11);
    return result;
}

void x86os_exit(int status) {
    (void)x86os_syscall(X86OS_SYS_EXIT, (uintptr_t)status, 0, 0);
    for (;;) {
        __asm__ volatile("pause");
    }
}
