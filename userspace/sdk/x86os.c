/**
 * @file userspace/sdk/x86os.c
 * @brief Typsichere Ring-3-Wrapper der REIST-Syscall-ABI.
 *
 * Layer: Ring-3 SDK and protocol support.
 * Contract: Größen, Versionen und Pufferbereiche werden vor Lesen, Schreiben oder Syscall geprüft.
 * Safety: Parser und Formatierung sind kapazitätsbegrenzt; Fehler erzeugen keine partiellen Ausgaben.
 */
#include "x86os.h"

_Static_assert(sizeof(x86os_memory_stats_t) == 120U,
               "memory statistics ABI size changed");
_Static_assert(sizeof(x86os_scheduler_stats_t) == 32U,
               "scheduler statistics ABI size changed");
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
_Static_assert(sizeof(x86os_usb_diagnostics_t) == 208U,
               "USB diagnostics ABI size changed");
_Static_assert(sizeof(x86os_ipc_message_t) == 140U,
               "IPC message ABI size changed");
_Static_assert(sizeof(x86os_admin_storage_request_t) == 104U,
               "admin storage request ABI changed");
_Static_assert(sizeof(x86os_admin_storage_result_t) == 128U,
               "admin storage result ABI changed");
_Static_assert(sizeof(x86os_component_request_t) == 24U,
               "component control request ABI changed");
_Static_assert(sizeof(x86os_component_result_t) == 56U,
               "component control result ABI changed");

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
    char digits[10];
    unsigned count = 0U;
    uint32_t magnitude;
    if (value < 0) {
        x86os_putchar('-');
        magnitude = 0U - (uint32_t)value;
    } else {
        magnitude = (uint32_t)value;
    }
    do {
        digits[count++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U && count < sizeof(digits));
    while (count != 0U) x86os_putchar(digits[--count]);
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

int x86os_scheduler_stats(x86os_scheduler_stats_t *stats) {
    return (int)x86os_syscall(X86OS_SYS_SCHEDULER_STATS,
                              (uintptr_t)stats, sizeof(*stats),
                              X86OS_SCHEDULER_STATS_VERSION);
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

_Static_assert(sizeof(x86os_reist_icmp_echo_reply_t) == 16U,
               "REIST ICMP echo reply ABI changed");

int x86os_reist_send_icmp_echo_reply(
        const x86os_reist_icmp_echo_reply_t *reply) {
    return (int)x86os_syscall(X86OS_SYS_REIST_ICMP_ECHO_REPLY,
                              (uintptr_t)reply, 0, 0);
}

_Static_assert(sizeof(x86os_reist_icmp_ingress_t) == 40U,
               "REIST ICMP ingress ABI size changed");

int x86os_reist_icmp_ingress(const x86os_reist_icmp_ingress_t *ingress,
                             const uint8_t *data) {
    return (int)x86os_syscall(X86OS_SYS_REIST_ICMP_INGRESS,
                              (uint32_t)(uintptr_t)ingress,
                              (uint32_t)(uintptr_t)data, 0U);
}

_Static_assert(sizeof(x86os_reist_dhcp_commit_t) == 16U,
               "REIST DHCP commit ABI size changed");

int x86os_reist_commit_dhcp(const x86os_reist_dhcp_commit_t *commit) {
    return (int)x86os_syscall(X86OS_SYS_REIST_DHCP_COMMIT,
                              (uint32_t)(uintptr_t)commit, 0U, 0U);
}

_Static_assert(sizeof(x86os_reist_dhcp_renew_request_t) == 16U,
               "REIST DHCP renew request ABI size changed");

int x86os_reist_renew_dhcp(
        const x86os_reist_dhcp_renew_request_t *request) {
    return (int)x86os_syscall(X86OS_SYS_REIST_DHCP_RENEW,
                              (uint32_t)(uintptr_t)request, 0U, 0U);
}

_Static_assert(sizeof(x86os_reist_network_frame_t) == 1536U,
               "REIST network frame ABI size changed");

int x86os_reist_receive_network_frame(x86os_reist_network_frame_t *frame) {
    return (int)x86os_syscall(X86OS_SYS_REIST_NETWORK_FRAME,
                              (uint32_t)(uintptr_t)frame, 0U, 0U);
}

_Static_assert(sizeof(x86os_reist_udp_echo_reply_t) == 16U,
               "REIST UDP echo reply ABI size changed");

int x86os_reist_send_udp_echo_reply(
        const x86os_reist_udp_echo_reply_t *reply) {
    return (int)x86os_syscall(X86OS_SYS_REIST_UDP_ECHO_REPLY,
                              (uint32_t)(uintptr_t)reply, 0U, 0U);
}

_Static_assert(sizeof(x86os_reist_udp_bind_request_t) == 16U,
               "REIST UDP bind request ABI size changed");
_Static_assert(sizeof(x86os_reist_udp_reply_t) == 16U,
               "REIST UDP reply ABI size changed");
_Static_assert(sizeof(x86os_reist_udp_ingress_t) == 40U,
               "REIST UDP ingress ABI size changed");

int x86os_reist_udp_bind(const x86os_reist_udp_bind_request_t *request,
                         x86os_reist_udp_binding_t *binding) {
    return (int)x86os_syscall(X86OS_SYS_REIST_UDP_BIND,
                              (uint32_t)(uintptr_t)request,
                              (uint32_t)(uintptr_t)binding, 0U);
}

int x86os_reist_udp_unbind(x86os_reist_udp_binding_t binding) {
    return (int)x86os_syscall(X86OS_SYS_REIST_UDP_UNBIND, binding, 0U, 0U);
}

int x86os_reist_udp_reply(const x86os_reist_udp_reply_t *reply) {
    return (int)x86os_syscall(X86OS_SYS_REIST_UDP_REPLY,
                              (uint32_t)(uintptr_t)reply, 0U, 0U);
}

int x86os_reist_udp_ingress(x86os_reist_udp_ingress_t *ingress,
                            const uint8_t *data) {
    return (int)x86os_syscall(X86OS_SYS_REIST_UDP_INGRESS,
                              (uint32_t)(uintptr_t)ingress,
                              (uint32_t)(uintptr_t)data, 0U);
}

_Static_assert(sizeof(x86os_reist_dhcp_ingress_t) == 52U,
               "REIST DHCP ingress ABI size changed");

int x86os_reist_dhcp_ingress(
        const x86os_reist_dhcp_ingress_t *ingress) {
    return (int)x86os_syscall(X86OS_SYS_REIST_DHCP_INGRESS,
                              (uint32_t)(uintptr_t)ingress, 0U, 0U);
}

_Static_assert(sizeof(x86os_reist_dhcp_boot_start_t) == 8U,
               "REIST DHCP boot start ABI size changed");

int x86os_reist_start_dhcp_boot(
        const x86os_reist_dhcp_boot_start_t *request) {
    return (int)x86os_syscall(X86OS_SYS_REIST_DHCP_BOOT_START,
                              (uint32_t)(uintptr_t)request, 0U, 0U);
}

int x86os_network_arp_resolve(uint32_t target_ip) {
    return (int)x86os_syscall(X86OS_SYS_NETWORK_ARP_RESOLVE,
                              target_ip, 0, 0);
}

_Static_assert(sizeof(x86os_network_control_request_t) == 44U,
               "network control request ABI size changed");
_Static_assert(sizeof(x86os_network_control_result_t) == 64U,
               "network control result ABI size changed");

int x86os_network_control(const x86os_network_control_request_t *request,
                          x86os_network_control_result_t *result) {
    return (int)x86os_syscall(X86OS_SYS_NETWORK_CONTROL,
                              (uintptr_t)request, (uintptr_t)result, 0U);
}

_Static_assert(sizeof(x86os_udp_socket_control_t) == 32U,
               "UDP socket control ABI changed");
_Static_assert(sizeof(x86os_udp_datagram_t) == 28U,
               "UDP datagram ABI changed");

static int udp_socket_control(uint32_t operation, x86os_udp_socket_t socket,
                              uint16_t port,
                              x86os_udp_socket_control_t *result) {
    x86os_udp_socket_control_t control = {
        .version = X86OS_UDP_SOCKET_VERSION,
        .struct_size = sizeof(control), .operation = operation,
        .socket = socket, .port = port,
    };
    int rc = (int)x86os_syscall(X86OS_SYS_UDP_SOCKET_CONTROL,
                                (uintptr_t)&control, 0U, 0U);
    if (rc == 0 && result != NULL) *result = control;
    return rc;
}

int x86os_udp_socket_open(x86os_udp_socket_t *socket_out) {
    if (socket_out == NULL) return -22;
    x86os_udp_socket_control_t result;
    int rc = udp_socket_control(X86OS_UDP_SOCKET_OPEN, 0U, 0U, &result);
    if (rc == 0) *socket_out = result.socket;
    return rc;
}
int x86os_udp_socket_bind(x86os_udp_socket_t socket, uint16_t port) {
    return udp_socket_control(X86OS_UDP_SOCKET_BIND, socket, port, NULL);
}
int x86os_udp_socket_close(x86os_udp_socket_t socket) {
    return udp_socket_control(X86OS_UDP_SOCKET_CLOSE, socket, 0U, NULL);
}
int x86os_udp_socket_stats(x86os_udp_socket_control_t *stats_out) {
    return stats_out == NULL ? -22 :
        udp_socket_control(X86OS_UDP_SOCKET_STATS, 0U, 0U, stats_out);
}
int x86os_udp_sendto(const x86os_udp_datagram_t *datagram,
                     const void *data) {
    return (int)x86os_syscall(X86OS_SYS_UDP_SOCKET_SENDTO,
                              (uintptr_t)datagram, (uintptr_t)data, 0U);
}
int x86os_udp_recvfrom(x86os_udp_datagram_t *datagram, void *data) {
    return (int)x86os_syscall(X86OS_SYS_UDP_SOCKET_RECVFROM,
                              (uintptr_t)datagram, (uintptr_t)data, 0U);
}
int x86os_udp_socket_ingress(const x86os_udp_datagram_t *datagram,
                             const void *data) {
    return (int)x86os_syscall(X86OS_SYS_UDP_SOCKET_INGRESS,
                              (uintptr_t)datagram, (uintptr_t)data, 0U);
}

_Static_assert(sizeof(x86os_tcp_socket_control_t) == 32U,
               "TCP socket control ABI changed");
_Static_assert(sizeof(x86os_tcp_connect_t) == 24U,
               "TCP connect ABI changed");
_Static_assert(sizeof(x86os_tcp_io_t) == 20U, "TCP I/O ABI changed");
_Static_assert(sizeof(x86os_tcp_listen_t) == 20U,
               "TCP listen ABI changed");
_Static_assert(sizeof(x86os_tcp_accept_t) == 28U,
               "TCP accept ABI changed");
_Static_assert(sizeof(x86os_tcp_segment_t) == 36U,
               "TCP segment ABI changed");

static int tcp_socket_control(uint32_t operation, x86os_tcp_socket_t socket,
                              uint32_t timeout_ms,
                              x86os_tcp_socket_control_t *result) {
    x86os_tcp_socket_control_t control = {
        .version = X86OS_TCP_SOCKET_VERSION,
        .struct_size = sizeof(control), .operation = operation,
        .socket = socket, .timeout_ms = timeout_ms,
    };
    int rc = (int)x86os_syscall(X86OS_SYS_TCP_SOCKET_CONTROL,
                                (uintptr_t)&control, 0U, 0U);
    if (rc == 0 && result != NULL) *result = control;
    return rc;
}
int x86os_tcp_socket_open(x86os_tcp_socket_t *socket_out) {
    if (socket_out == NULL) return -22;
    x86os_tcp_socket_control_t result;
    int rc = tcp_socket_control(X86OS_TCP_SOCKET_OPEN, 0U, 0U, &result);
    if (rc == 0) *socket_out = result.socket; return rc;
}
int x86os_tcp_socket_close(x86os_tcp_socket_t socket, uint32_t timeout_ms) {
    return tcp_socket_control(X86OS_TCP_SOCKET_CLOSE, socket, timeout_ms, NULL);
}
int x86os_tcp_socket_stats(x86os_tcp_socket_control_t *stats_out) {
    return stats_out == NULL ? -22 :
        tcp_socket_control(X86OS_TCP_SOCKET_STATS, 0U, 0U, stats_out);
}
int x86os_tcp_connect(const x86os_tcp_connect_t *request) {
    return (int)x86os_syscall(X86OS_SYS_TCP_SOCKET_CONNECT,
                              (uintptr_t)request, 0U, 0U);
}
int x86os_tcp_listen(const x86os_tcp_listen_t *request) {
    return (int)x86os_syscall(X86OS_SYS_TCP_SOCKET_LISTEN,
                              (uintptr_t)request, 0U, 0U);
}
int x86os_tcp_accept(x86os_tcp_accept_t *request) {
    return (int)x86os_syscall(X86OS_SYS_TCP_SOCKET_ACCEPT,
                              (uintptr_t)request, 0U, 0U);
}
int x86os_tcp_send(const x86os_tcp_io_t *request, const void *data) {
    return (int)x86os_syscall(X86OS_SYS_TCP_SOCKET_SEND,
                              (uintptr_t)request, (uintptr_t)data, 0U);
}
int x86os_tcp_receive(x86os_tcp_io_t *request, void *data) {
    return (int)x86os_syscall(X86OS_SYS_TCP_SOCKET_RECEIVE,
                              (uintptr_t)request, (uintptr_t)data, 0U);
}
int x86os_tcp_socket_ingress(const x86os_tcp_segment_t *segment,
                             const void *data) {
    return (int)x86os_syscall(X86OS_SYS_TCP_SOCKET_INGRESS,
                              (uintptr_t)segment, (uintptr_t)data, 0U);
}

_Static_assert(sizeof(x86os_storage_submit_t) == 28U,
               "storage submit ABI changed");
_Static_assert(sizeof(x86os_storage_descriptor_t) == 28U,
               "storage descriptor ABI changed");

int x86os_storage_bind(void) {
    return (int)x86os_syscall(X86OS_SYS_STORAGE_BIND, 0, 0, 0);
}

int x86os_storage_submit(const x86os_storage_submit_t *request,
                         const void *data, x86os_storage_handle_t *handle) {
    return (int)x86os_syscall(X86OS_SYS_STORAGE_SUBMIT, (uintptr_t)request,
                              (uintptr_t)data, (uintptr_t)handle);
}

int x86os_storage_claim(x86os_storage_descriptor_t *request, void *data) {
    return (int)x86os_syscall(X86OS_SYS_STORAGE_CLAIM, (uintptr_t)request,
                              (uintptr_t)data, 0);
}

int x86os_storage_block_read(uint32_t resource, uint32_t block, void *data) {
    return (int)x86os_syscall(X86OS_SYS_STORAGE_BLOCK_READ, resource, block,
                              (uintptr_t)data);
}

int x86os_storage_block_write(uint32_t resource, uint32_t block,
                              const void *data) {
    return (int)x86os_syscall(X86OS_SYS_STORAGE_BLOCK_WRITE, resource, block,
                              (uintptr_t)data);
}

int x86os_storage_block_flush(uint32_t resource) {
    return (int)x86os_syscall(X86OS_SYS_STORAGE_BLOCK_FLUSH, resource, 0U, 0U);
}

int x86os_storage_media_commit(uint32_t resource, uint32_t *fingerprint) {
    return (int)x86os_syscall(X86OS_SYS_STORAGE_MEDIA_COMMIT, resource,
                              (uintptr_t)fingerprint, 0U);
}

int x86os_storage_format_probe(uint32_t resource, uint32_t sector) {
    return (int)x86os_syscall(X86OS_SYS_STORAGE_FORMAT_PROBE, resource,
                              sector, 0U);
}

int x86os_storage_maintenance_acquire(uint32_t resource,
                                      uint32_t media_fingerprint,
                                      uint32_t *token) {
    return (int)x86os_syscall(X86OS_SYS_STORAGE_MAINT_ACQUIRE, resource,
                              media_fingerprint, (uintptr_t)token);
}

int x86os_storage_maintenance_renew(uint32_t resource, uint32_t token,
                                    uint32_t media_fingerprint) {
    return (int)x86os_syscall(X86OS_SYS_STORAGE_MAINT_RENEW, resource, token,
                              media_fingerprint);
}

int x86os_storage_maintenance_release(uint32_t resource, uint32_t token) {
    return (int)x86os_syscall(X86OS_SYS_STORAGE_MAINT_RELEASE, resource, token,
                              0);
}

int x86os_storage_complete(x86os_storage_handle_t handle, int32_t result,
                           const void *data) {
    return (int)x86os_syscall(X86OS_SYS_STORAGE_COMPLETE, handle,
                              (uintptr_t)result, (uintptr_t)data);
}

int x86os_storage_collect(x86os_storage_handle_t handle, int32_t *result,
                          void *data) {
    return (int)x86os_syscall(X86OS_SYS_STORAGE_COLLECT, handle,
                              (uintptr_t)result, (uintptr_t)data);
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

int x86os_display_activate(void) {
    x86os_display_control_t request = {
        .version = X86OS_DISPLAY_CONTROL_VERSION,
        .struct_size = sizeof(request),
        .operation = X86OS_DISPLAY_ACTIVATE,
        .reserved = 0U
    };
    return (int)x86os_syscall(X86OS_SYS_DISPLAY_CONTROL,
                              (uintptr_t)&request, 0, 0);
}

int x86os_display_deactivate(void) {
    x86os_display_control_t request = {
        .version = X86OS_DISPLAY_CONTROL_VERSION,
        .struct_size = sizeof(request),
        .operation = X86OS_DISPLAY_DEACTIVATE,
        .reserved = 0U
    };
    return (int)x86os_syscall(X86OS_SYS_DISPLAY_CONTROL,
                              (uintptr_t)&request, 0, 0);
}

int x86os_display_frame_begin(uint32_t* serial) {
    if (!serial) return -22;
    x86os_display_frame_t request = {
        .version = X86OS_DISPLAY_CONTROL_VERSION,
        .struct_size = sizeof(request),
        .operation = X86OS_DISPLAY_FRAME_BEGIN,
        .flags = 0U,
        .serial = 0U,
        .reserved = 0U
    };
    int result = (int)x86os_syscall(X86OS_SYS_DISPLAY_CONTROL,
                                    (uintptr_t)&request, 0, 0);
    if (result == 0) {
        if (request.serial == 0U) return -5;
        *serial = request.serial;
    }
    return result;
}

int x86os_display_frame_commit(uint32_t serial) {
    if (serial == 0U) return -22;
    x86os_display_frame_t request = {
        .version = X86OS_DISPLAY_CONTROL_VERSION,
        .struct_size = sizeof(request),
        .operation = X86OS_DISPLAY_FRAME_COMMIT,
        .flags = 0U,
        .serial = serial,
        .reserved = 0U
    };
    return (int)x86os_syscall(X86OS_SYS_DISPLAY_CONTROL,
                              (uintptr_t)&request, 0, 0);
}

int x86os_display_frame_cancel(uint32_t serial) {
    if (serial == 0U) return -22;
    x86os_display_frame_t request = {
        .version = X86OS_DISPLAY_CONTROL_VERSION,
        .struct_size = sizeof(request),
        .operation = X86OS_DISPLAY_FRAME_CANCEL,
        .flags = 0U,
        .serial = serial,
        .reserved = 0U
    };
    return (int)x86os_syscall(X86OS_SYS_DISPLAY_CONTROL,
                              (uintptr_t)&request, 0, 0);
}

int x86os_mouse_event(x86os_mouse_event_t* event) {
    if (!event) return -22;
    event->version = X86OS_MOUSE_EVENT_VERSION;
    event->struct_size = sizeof(*event);
    return (int)x86os_syscall(X86OS_SYS_MOUSE_EVENT,
                              (uintptr_t)event, 0, 0);
}

int x86os_pointer_update(int32_t x, int32_t y, uint32_t visible) {
    return (int)x86os_syscall(X86OS_SYS_POINTER_UPDATE, (uintptr_t)x,
                              (uintptr_t)y, visible);
}

int x86os_usb_diagnostics(x86os_usb_diagnostics_t* diagnostics) {
    if (!diagnostics) return -22;
    diagnostics->version = X86OS_USB_DIAGNOSTICS_VERSION;
    diagnostics->struct_size = sizeof(*diagnostics);
    return (int)x86os_syscall(X86OS_SYS_USB_DIAGNOSTICS,
                              (uintptr_t)diagnostics, 0, 0);
}

int x86os_touch(const char* path) {
    return (int)x86os_syscall(X86OS_SYS_TOUCH, (uintptr_t)path, 0, 0);
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

int x86os_partition_create(const x86os_partition_request_t *request) {
    if (request == NULL) return -22;
    return (int)x86os_syscall(X86OS_SYS_PARTITION_CREATE,
                              (uintptr_t)request, 0U, 0U);
}

int x86os_drive_status(uint32_t index, x86os_drive_status_t* status) {
    if (status == NULL) return -22;
    status->version = X86OS_DRIVE_STATUS_VERSION;
    status->struct_size = sizeof(*status);
    return (int)x86os_syscall(X86OS_SYS_DRIVE_STATUS, index,
                              (uintptr_t)status, 0);
}

int x86os_admin_storage(const x86os_admin_storage_request_t* request,
                        x86os_admin_storage_result_t* result) {
    if (request == NULL || result == NULL) return -22;
    return (int)x86os_syscall(X86OS_SYS_ADMIN_STORAGE,
                              (uintptr_t)request, (uintptr_t)result, 0U);
}

int x86os_component_control(const x86os_component_request_t* request,
                            x86os_component_result_t* result) {
    if (request == NULL || result == NULL) return -22;
    return (int)x86os_syscall(X86OS_SYS_COMPONENT_CONTROL,
                              (uintptr_t)request, (uintptr_t)result, 0U);
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
