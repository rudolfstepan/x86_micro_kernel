#include "x86os.h"

_Static_assert(sizeof(x86os_memory_stats_t) == 88U,
               "memory statistics ABI size changed");
_Static_assert(offsetof(x86os_memory_stats_t, detected_usable_bytes) == 8U,
               "memory statistics ABI header changed");
_Static_assert(sizeof(x86os_network_probe_stats_t) == 24U,
               "network probe statistics ABI size changed");
_Static_assert(sizeof(x86os_display_info_t) == 56U,
               "display information ABI size changed");
_Static_assert(sizeof(x86os_display_rect_t) == 28U,
               "display rectangle ABI size changed");
_Static_assert(sizeof(x86os_display_text_t) == 32U,
               "display text ABI size changed");
_Static_assert(sizeof(x86os_ipc_message_t) == 140U,
               "IPC message ABI size changed");

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

int x86os_sleep_ms(uint32_t milliseconds) {
    return (int)x86os_syscall(X86OS_SYS_SLEEP_MS, milliseconds, 0, 0);
}

int x86os_yield(void) {
    return (int)x86os_syscall(X86OS_SYS_YIELD, 0, 0, 0);
}

int x86os_monotonic_ms(uint64_t* value) {
    return (int)x86os_syscall(X86OS_SYS_MONOTONIC_MS,
                              (uintptr_t)value, 0, 0);
}

int x86os_memory_stats(x86os_memory_stats_t* stats) {
    return (int)x86os_syscall(X86OS_SYS_MEMORY_STATS,
                              (uintptr_t)stats, sizeof(*stats),
                              X86OS_MEMORY_STATS_VERSION);
}

int x86os_ipc_create(x86os_ipc_handle_t* handle) {
    return (int)x86os_syscall(X86OS_SYS_IPC_CREATE,
                              (uintptr_t)handle, 0, 0);
}

int x86os_ipc_send(x86os_ipc_handle_t handle,
                   const x86os_ipc_message_t* message) {
    return (int)x86os_syscall(X86OS_SYS_IPC_SEND, handle,
                              (uintptr_t)message, 0);
}

int x86os_ipc_receive(x86os_ipc_handle_t handle,
                      x86os_ipc_message_t* message) {
    return (int)x86os_syscall(X86OS_SYS_IPC_RECEIVE, handle,
                              (uintptr_t)message, 0);
}

int x86os_ipc_send_timeout(x86os_ipc_handle_t handle,
                           const x86os_ipc_message_t* message,
                           uint32_t timeout_ms) {
    return (int)x86os_syscall(X86OS_SYS_IPC_SEND_TIMEOUT, handle,
                              (uintptr_t)message, timeout_ms);
}

int x86os_ipc_receive_timeout(x86os_ipc_handle_t handle,
                              x86os_ipc_message_t* message,
                              uint32_t timeout_ms) {
    return (int)x86os_syscall(X86OS_SYS_IPC_RECEIVE_TIMEOUT, handle,
                              (uintptr_t)message, timeout_ms);
}

int x86os_ipc_close(x86os_ipc_handle_t handle) {
    return (int)x86os_syscall(X86OS_SYS_IPC_CLOSE, handle, 0, 0);
}

int x86os_ipc_release(x86os_ipc_handle_t handle) {
    return (int)x86os_syscall(X86OS_SYS_IPC_RELEASE, handle, 0, 0);
}

int x86os_network_probe(void) {
    return (int)x86os_syscall(X86OS_SYS_NETWORK_PROBE, 0, 0, 0);
}

int x86os_network_probe_id(uint32_t *probe_id) {
    return (int)x86os_syscall(X86OS_SYS_NETWORK_PROBE_ID,
                              (uintptr_t)probe_id, 0, 0);
}

int x86os_network_probe_stats(x86os_network_probe_stats_t *stats) {
    return (int)x86os_syscall(X86OS_SYS_NETWORK_PROBE_STATS,
                              (uintptr_t)stats, sizeof(*stats),
                              X86OS_NETWORK_PROBE_STATS_VERSION);
}

_Static_assert(sizeof(x86os_reist_arp_binding_t) == 24U,
               "REIST ARP binding ABI changed");

int x86os_reist_commit_arp_binding(
        const x86os_reist_arp_binding_t *binding) {
    return (int)x86os_syscall(X86OS_SYS_REIST_ARP_BINDING,
                              (uintptr_t)binding, 0, 0);
}

_Static_assert(sizeof(x86os_reist_arp_reply_t) == 24U,
               "REIST ARP reply ABI changed");

int x86os_reist_send_arp_reply(const x86os_reist_arp_reply_t *reply) {
    return (int)x86os_syscall(X86OS_SYS_REIST_ARP_REPLY,
                              (uintptr_t)reply, 0, 0);
}

_Static_assert(sizeof(x86os_reist_arp_resolution_t) == 16U,
               "REIST ARP resolution ABI changed");

int x86os_reist_send_arp_request(
        const x86os_reist_arp_resolution_t *request) {
    return (int)x86os_syscall(X86OS_SYS_REIST_ARP_RESOLUTION,
                              (uintptr_t)request, 0, 0);
}

int x86os_network_arp_resolve(uint32_t target_ip) {
    return (int)x86os_syscall(X86OS_SYS_NETWORK_ARP_RESOLVE,
                              target_ip, 0, 0);
}

int x86os_ipc_delegate(x86os_ipc_handle_t handle, int target_pid,
                       uint32_t rights) {
    return (int)x86os_syscall(X86OS_SYS_IPC_DELEGATE, handle,
                              (uintptr_t)target_pid, rights);
}

int x86os_reist_report(uint32_t report_type, uint32_t value) {
    return (int)x86os_syscall(X86OS_SYS_REIST_REPORT, report_type, value, 0);
}

int x86os_service_connect(uint32_t service_id,
                          x86os_ipc_handle_t* handle) {
    return (int)x86os_syscall(X86OS_SYS_SERVICE_CONNECT, service_id,
                              (uintptr_t)handle, 0);
}

int x86os_display_info(x86os_display_info_t* info) {
    if (!info) return -22;
    info->version = X86OS_DISPLAY_ABI_VERSION;
    info->struct_size = sizeof(*info);
    return (int)x86os_syscall(X86OS_SYS_DISPLAY_INFO,
                              (uintptr_t)info, 0, 0);
}

int x86os_fill_rect(int32_t x, int32_t y, uint32_t width, uint32_t height,
                    uint32_t rgb) {
    x86os_display_rect_t rect = {
        .version = X86OS_DISPLAY_ABI_VERSION,
        .struct_size = sizeof(rect),
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .rgb = rgb
    };
    return (int)x86os_syscall(X86OS_SYS_FILL_RECT,
                              (uintptr_t)&rect, 0, 0);
}

int x86os_draw_text_pixels(int32_t x, int32_t y, const char* text,
                           size_t length, uint32_t foreground_rgb,
                           uint32_t background_rgb) {
    if ((!text && length != 0) || length > X86OS_DISPLAY_MAX_TEXT) return -22;
    x86os_display_text_t request = {
        .version = X86OS_DISPLAY_ABI_VERSION,
        .struct_size = sizeof(request),
        .x = x,
        .y = y,
        .foreground_rgb = foreground_rgb,
        .background_rgb = background_rgb,
        .text_address = (uint32_t)(uintptr_t)text,
        .text_length = (uint32_t)length
    };
    return (int)x86os_syscall(X86OS_SYS_DRAW_TEXT,
                              (uintptr_t)&request, 0, 0);
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

int x86os_fsync(int descriptor) {
    return (int)x86os_syscall(X86OS_SYS_FSYNC, (uintptr_t)descriptor, 0, 0);
}

int x86os_unlink(const char* path) {
    return (int)x86os_syscall(X86OS_SYS_UNLINK, (uintptr_t)path, 0, 0);
}

int x86os_rename(const char* old_path, const char* new_path) {
    return (int)x86os_syscall(X86OS_SYS_RENAME, (uintptr_t)old_path,
                              (uintptr_t)new_path, 0);
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
