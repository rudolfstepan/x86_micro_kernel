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
_Static_assert(sizeof(x86os_runtime_timing_t) == 72U,
               "runtime timing statistics ABI changed");
_Static_assert(sizeof(x86os_boot_status_t) == 40U,
               "boot status ABI changed");
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
_Static_assert(sizeof(x86os_display_text_clipped_t) == 48U,
               "clipped display text ABI size changed");
_Static_assert(sizeof(x86os_display_pixels_t) == 48U,
               "display pixels ABI size changed");
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
_Static_assert(sizeof(x86os_device_region_request_t) == 32U,
               "device region request ABI changed");
_Static_assert(sizeof(x86os_device_region_info_t) == 48U,
               "device region info ABI changed");
_Static_assert(sizeof(x86os_device_irq_request_t) == 32U,
               "device IRQ request ABI changed");
_Static_assert(sizeof(x86os_device_dma_request_t) == 32U,
               "device DMA request ABI changed");
_Static_assert(sizeof(x86os_device_action_request_t) == 32U,
               "device action request ABI changed");
_Static_assert(sizeof(x86os_device_resource_request_t) == 32U,
               "device resource request ABI changed");
_Static_assert(sizeof(x86os_device_resource_result_t) == 32U,
               "device resource result ABI changed");
_Static_assert(sizeof(x86os_device_irq_message_t) == 32U,
               "device IRQ message ABI changed");
_Static_assert(sizeof(x86os_device_irq_completion_t) == 32U,
               "device IRQ completion ABI changed");
_Static_assert(sizeof(x86os_device_dma_transfer_t) == 32U,
               "device DMA transfer ABI changed");
_Static_assert(sizeof(x86os_device_dma_info_t) == 32U,
               "device DMA info ABI changed");
_Static_assert(sizeof(x86os_device_dma_pool_stats_t) == 32U,
               "device DMA pool statistics ABI changed");
_Static_assert(sizeof(x86os_device_dma_descriptor_t) == 32U,
               "device DMA descriptor ABI changed");
_Static_assert(sizeof(x86os_device_region_access_t) == 32U,
               "device region access ABI changed");
_Static_assert(sizeof(x86os_device_region_value_t) == 32U,
               "device region value ABI changed");
_Static_assert(sizeof(x86os_device_region_dma_address_t) == 32U,
               "device region DMA address ABI changed");
_Static_assert(sizeof(x86os_device_driver_bootstrap_t) == 32U,
               "device driver bootstrap ABI changed");
_Static_assert(sizeof(x86os_device_driver_report_t) == 32U,
               "device driver report ABI changed");
_Static_assert(sizeof(x86os_device_resource_status_t) == 40U,
               "device resource status ABI changed");
_Static_assert(sizeof(x86os_device_iommu_status_t) == 36U,
               "device IOMMU status ABI changed");

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

int x86os_runtime_timing(x86os_runtime_timing_t *stats) {
    return (int)x86os_syscall(X86OS_SYS_RUNTIME_TIMING,
                              (uintptr_t)stats, sizeof(*stats),
                              X86OS_RUNTIME_TIMING_VERSION);
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
_Static_assert(sizeof(x86os_storage_descriptor_v2_t) == 40U,
               "storage descriptor v2 ABI changed");
_Static_assert(sizeof(x86os_vfs_shadow_frame_t) == X86OS_STORAGE_BLOCK_SIZE,
               "VFS shadow frame must fill one protected request payload");

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

int x86os_storage_claim_identity(x86os_storage_descriptor_v2_t *request,
                                 void *data) {
    return (int)x86os_syscall(X86OS_SYS_STORAGE_CLAIM_IDENTITY,
                              (uintptr_t)request, (uintptr_t)data, 0U);
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

int x86os_storage_cancel(x86os_storage_handle_t handle) {
    return (int)x86os_syscall(X86OS_SYS_STORAGE_CANCEL, handle, 0U, 0U);
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

int x86os_draw_text_pixels_clipped(
    int32_t x, int32_t y, const char *text, size_t length,
    uint32_t foreground_rgb, uint32_t background_rgb,
    int32_t clip_x, int32_t clip_y, uint32_t clip_width,
    uint32_t clip_height) {
    if ((!text && length != 0U) || length > X86OS_DISPLAY_MAX_TEXT)
        return -22;
    x86os_display_text_clipped_t request = {
        .version = X86OS_DISPLAY_TEXT_CLIPPED_VERSION,
        .struct_size = sizeof(request),
        .x = x,
        .y = y,
        .foreground_rgb = foreground_rgb,
        .background_rgb = background_rgb,
        .text_address = (uint32_t)(uintptr_t)text,
        .text_length = (uint32_t)length,
        .clip_x = clip_x,
        .clip_y = clip_y,
        .clip_width = clip_width,
        .clip_height = clip_height,
    };
    return (int)x86os_syscall(X86OS_SYS_DRAW_TEXT_CLIPPED,
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

int x86os_open_flags(const char* path, uint32_t flags) {
    return (int)x86os_syscall(X86OS_SYS_OPEN_FLAGS, (uintptr_t)path, flags, 0);
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

int32_t x86os_lseek(int descriptor, int32_t offset, uint32_t whence) {
    return (int32_t)x86os_syscall(X86OS_SYS_LSEEK, (uintptr_t)descriptor,
                                  (uintptr_t)offset, whence);
}

int x86os_fstat(int descriptor, x86os_file_info_t* info) {
    return (int)x86os_syscall(X86OS_SYS_FSTAT, (uintptr_t)descriptor,
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

int x86os_display_frame_stage_blit(uint32_t serial,
                                   uint32_t source_x, uint32_t source_y,
                                   uint32_t destination_x,
                                   uint32_t destination_y,
                                   uint32_t width, uint32_t height) {
    if (serial == 0U || width == 0U || height == 0U) return -22;
    x86os_display_blit_t request = {
        .version = X86OS_DISPLAY_CONTROL_VERSION,
        .struct_size = sizeof(request),
        .operation = X86OS_DISPLAY_FRAME_STAGE_BLIT,
        .flags = 0U,
        .serial = serial,
        .reserved = 0U,
        .source_x = source_x,
        .source_y = source_y,
        .destination_x = destination_x,
        .destination_y = destination_y,
        .width = width,
        .height = height,
    };
    return (int)x86os_syscall(X86OS_SYS_DISPLAY_CONTROL,
                              (uintptr_t)&request, 0, 0);
}

int x86os_display_frame_mark_accelerated(uint32_t serial) {
    if (serial == 0U) return -22;
    x86os_display_frame_t request = {
        .version = X86OS_DISPLAY_CONTROL_VERSION,
        .struct_size = sizeof(request),
        .operation = X86OS_DISPLAY_FRAME_MARK_ACCELERATED,
        .flags = 0U,
        .serial = serial,
        .reserved = 0U,
    };
    return (int)x86os_syscall(X86OS_SYS_DISPLAY_CONTROL,
                              (uintptr_t)&request, 0, 0);
}

int x86os_display_driver_command(x86os_display_driver_request_t *request) {
    if (request == NULL ||
        request->version != X86OS_DISPLAY_CONTROL_VERSION ||
        request->struct_size < sizeof(*request) ||
        request->operation != X86OS_DISPLAY_DRIVER_COMMAND ||
        request->flags != 0U || request->device == 0U ||
        request->command == 0U)
        return -22;
    return (int)x86os_syscall(X86OS_SYS_DISPLAY_CONTROL,
                              (uintptr_t)request, 0, 0);
}

int x86os_draw_pixels(int32_t x, int32_t y, uint32_t width, uint32_t height,
                      const uint32_t *pixels, uint32_t stride_pixels) {
    if (!pixels || width == 0U || height == 0U || stride_pixels < width ||
        (uint64_t)stride_pixels * height > UINT32_MAX) return -22;
    x86os_display_pixels_t request = {
        .version = X86OS_DISPLAY_CONTROL_VERSION,
        .struct_size = sizeof(request),
        .operation = X86OS_DISPLAY_DRAW_PIXELS,
        .flags = 0U,
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .stride_pixels = stride_pixels,
        .pixels_address = (uint32_t)(uintptr_t)pixels,
        .pixel_count = stride_pixels * height,
        .reserved = 0U
    };
    return (int)x86os_syscall(X86OS_SYS_DISPLAY_CONTROL,
                              (uintptr_t)&request, 0, 0);
}

int x86os_display_surface_buffer_create(
    uint32_t width, uint32_t height, const uint32_t *pixels,
    uint32_t stride_pixels, uint32_t *buffer_id,
    uint32_t *buffer_generation) {
    if (!pixels || !buffer_id || !buffer_generation || width == 0U ||
        height == 0U || stride_pixels < width ||
        (uint64_t)stride_pixels * height > UINT32_MAX) return -22;
    x86os_display_surface_buffer_t request = {
        .version = X86OS_DISPLAY_CONTROL_VERSION,
        .struct_size = sizeof(request),
        .operation = X86OS_DISPLAY_SURFACE_BUFFER_CREATE,
        .flags = 0U,
        .buffer_id = 0U,
        .buffer_generation = 0U,
        .width = width,
        .height = height,
        .stride_pixels = stride_pixels,
        .pixels_address = (uint32_t)(uintptr_t)pixels,
        .pixel_count = stride_pixels * height,
        .reserved = 0U,
    };
    int result = (int)x86os_syscall(
        X86OS_SYS_DISPLAY_CONTROL, (uintptr_t)&request, 0, 0);
    if (result == 0) {
        if (request.buffer_id == 0U || request.buffer_generation == 0U)
            return -5;
        *buffer_id = request.buffer_id;
        *buffer_generation = request.buffer_generation;
    }
    return result;
}

int x86os_display_surface_buffer_destroy(
    uint32_t buffer_id, uint32_t buffer_generation) {
    if (buffer_id == 0U || buffer_generation == 0U) return -22;
    x86os_display_surface_buffer_t request = {
        .version = X86OS_DISPLAY_CONTROL_VERSION,
        .struct_size = sizeof(request),
        .operation = X86OS_DISPLAY_SURFACE_BUFFER_DESTROY,
        .flags = 0U,
        .buffer_id = buffer_id,
        .buffer_generation = buffer_generation,
        .reserved = 0U,
    };
    return (int)x86os_syscall(
        X86OS_SYS_DISPLAY_CONTROL, (uintptr_t)&request, 0, 0);
}

int x86os_display_surface_buffer_draw(
    int owner_pid, uint32_t owner_generation,
    uint32_t buffer_id, uint32_t buffer_generation,
    uint32_t source_x, uint32_t source_y,
    int32_t destination_x, int32_t destination_y,
    uint32_t width, uint32_t height) {
    if (owner_pid <= 0 || owner_generation == 0U || buffer_id == 0U ||
        buffer_generation == 0U || destination_x < 0 || destination_y < 0 ||
        width == 0U || height == 0U) return -22;
    x86os_display_surface_buffer_draw_t request = {
        .version = X86OS_DISPLAY_CONTROL_VERSION,
        .struct_size = sizeof(request),
        .operation = X86OS_DISPLAY_SURFACE_BUFFER_DRAW,
        .flags = 0U,
        .buffer_id = buffer_id,
        .buffer_generation = buffer_generation,
        .owner_pid = owner_pid,
        .owner_generation = owner_generation,
        .source_x = source_x,
        .source_y = source_y,
        .destination_x = destination_x,
        .destination_y = destination_y,
        .width = width,
        .height = height,
        .reserved = {0U, 0U},
    };
    return (int)x86os_syscall(
        X86OS_SYS_DISPLAY_CONTROL, (uintptr_t)&request, 0, 0);
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

int x86os_boot_status(x86os_boot_status_t *status) {
    if (status == NULL) return -22;
    return (int)x86os_syscall(X86OS_SYS_BOOT_STATUS,
                              (uintptr_t)status, 0U, 0U);
}

int x86os_process_identity(x86os_process_identity_t* identity) {
    if (identity == NULL) return -22;
    return (int)x86os_syscall(X86OS_SYS_PROCESS_IDENTITY,
                              (uintptr_t)identity, 0U, 0U);
}

int x86os_process_identity_of(int pid, x86os_process_identity_t* identity) {
    if (pid <= 0 || identity == NULL) return -22;
    return (int)x86os_syscall(X86OS_SYS_PROCESS_IDENTITY,
                              (uintptr_t)identity, (uintptr_t)pid, 0U);
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

int x86os_device_open_region(x86os_device_handle_t device,
                             uint32_t region_index, uint32_t rights,
                             x86os_device_region_info_t *region) {
    if (device == 0U || region == NULL) return -22;
    const x86os_device_region_request_t request = {
        .version = X86OS_DEVICE_ABI_VERSION,
        .struct_size = sizeof(request),
        .device = device,
        .region_index = region_index,
        .rights = rights,
    };
    return (int)x86os_syscall(X86OS_SYS_DEVICE_CONTROL,
        X86OS_DEVICE_CONTROL_REGION_OPEN, (uintptr_t)&request,
        (uintptr_t)region);
}

int x86os_device_bind_irq(x86os_device_handle_t device,
                          x86os_ipc_handle_t endpoint,
                          x86os_device_resource_result_t *resource) {
    if (device == 0U || endpoint == 0U || resource == NULL) return -22;
    const x86os_device_irq_request_t request = {
        .version = X86OS_DEVICE_ABI_VERSION,
        .struct_size = sizeof(request),
        .device = device,
        .endpoint_capability = endpoint,
    };
    return (int)x86os_syscall(X86OS_SYS_DEVICE_CONTROL,
        X86OS_DEVICE_CONTROL_IRQ_BIND, (uintptr_t)&request,
        (uintptr_t)resource);
}

int x86os_device_bind_dma(x86os_device_handle_t device,
                          uint32_t dma_capability,
                          x86os_device_resource_result_t *resource) {
    return x86os_device_bind_dma_direction(
        device, dma_capability,
        X86OS_DEVICE_DMA_TO_DEVICE | X86OS_DEVICE_DMA_FROM_DEVICE, resource);
}

int x86os_device_bind_dma_direction(
        x86os_device_handle_t device, uint32_t dma_capability,
        uint32_t direction, x86os_device_resource_result_t *resource) {
    if (device == 0U || resource == NULL || direction == 0U ||
        (direction & ~(X86OS_DEVICE_DMA_TO_DEVICE |
                       X86OS_DEVICE_DMA_FROM_DEVICE)) != 0U) return -22;
    const x86os_device_dma_request_t request = {
        .version = X86OS_DEVICE_ABI_VERSION,
        .struct_size = sizeof(request),
        .device = device,
        .dma_capability = dma_capability,
        .flags = direction,
    };
    return (int)x86os_syscall(X86OS_SYS_DEVICE_CONTROL,
        X86OS_DEVICE_CONTROL_DMA_BIND, (uintptr_t)&request,
        (uintptr_t)resource);
}

int x86os_device_activate(x86os_device_handle_t device) {
    if (device == 0U) return -22;
    const x86os_device_action_request_t request = {
        .version = X86OS_DEVICE_ABI_VERSION,
        .struct_size = sizeof(request),
        .device = device,
    };
    return (int)x86os_syscall(X86OS_SYS_DEVICE_CONTROL,
        X86OS_DEVICE_CONTROL_ACTIVATE, (uintptr_t)&request, 0U);
}

int x86os_device_deactivate(x86os_device_handle_t device) {
    if (device == 0U) return -22;
    const x86os_device_action_request_t request = {
        .version = X86OS_DEVICE_ABI_VERSION,
        .struct_size = sizeof(request),
        .device = device,
    };
    return (int)x86os_syscall(X86OS_SYS_DEVICE_CONTROL,
        X86OS_DEVICE_CONTROL_DEACTIVATE, (uintptr_t)&request, 0U);
}

int x86os_device_resource_status(x86os_device_resource_t resource,
                                 x86os_device_resource_status_t *status) {
    if (resource == 0U || status == NULL) return -22;
    const x86os_device_resource_request_t request = {
        .version = X86OS_DEVICE_ABI_VERSION,
        .struct_size = sizeof(request),
        .resource = resource,
    };
    return (int)x86os_syscall(X86OS_SYS_DEVICE_CONTROL,
        X86OS_DEVICE_CONTROL_RESOURCE_STATUS, (uintptr_t)&request,
        (uintptr_t)status);
}

int x86os_device_irq_complete(x86os_device_resource_t resource,
                              x86os_device_irq_completion_t *completion) {
    if (resource == 0U || completion == NULL) return -22;
    const x86os_device_resource_request_t request = {
        .version = X86OS_DEVICE_ABI_VERSION,
        .struct_size = sizeof(request),
        .resource = resource,
    };
    return (int)x86os_syscall(X86OS_SYS_DEVICE_CONTROL,
        X86OS_DEVICE_CONTROL_IRQ_COMPLETE, (uintptr_t)&request,
        (uintptr_t)completion);
}

int x86os_device_dma_info(x86os_device_resource_t resource,
                          x86os_device_dma_info_t *info) {
    if (resource == 0U || info == NULL) return -22;
    const x86os_device_resource_request_t request = {
        .version = X86OS_DEVICE_ABI_VERSION,
        .struct_size = sizeof(request),
        .resource = resource,
    };
    return (int)x86os_syscall(X86OS_SYS_DEVICE_CONTROL,
        X86OS_DEVICE_CONTROL_DMA_INFO, (uintptr_t)&request,
        (uintptr_t)info);
}

int x86os_device_dma_pool_stats(x86os_device_dma_pool_stats_t *stats) {
    if (stats == NULL) return -22;
    return (int)x86os_syscall(X86OS_SYS_DEVICE_CONTROL,
        X86OS_DEVICE_CONTROL_DMA_POOL_STATS, 0U, (uintptr_t)stats);
}

static int x86os_device_dma_transfer(
        uint32_t command, x86os_device_resource_t resource, uint32_t offset,
        void *data, uint32_t length) {
    uintptr_t address = (uintptr_t)data;
    if ((command != X86OS_DEVICE_CONTROL_DMA_WRITE &&
         command != X86OS_DEVICE_CONTROL_DMA_READ) || resource == 0U ||
        data == NULL || address > UINT32_MAX || length == 0U ||
        length > X86OS_DEVICE_DMA_TRANSFER_MAX ||
        offset < X86OS_DEVICE_DMA_DATA_OFFSET) return -22;
    const x86os_device_dma_transfer_t request = {
        .version = X86OS_DEVICE_ABI_VERSION,
        .struct_size = sizeof(request),
        .resource = resource,
        .offset = offset,
        .length = length,
        .user_buffer = (uint32_t)address,
    };
    return (int)x86os_syscall(X86OS_SYS_DEVICE_CONTROL, command,
                              (uintptr_t)&request, 0U);
}

int x86os_device_dma_write(x86os_device_resource_t resource, uint32_t offset,
                           const void *data, uint32_t length) {
    return x86os_device_dma_transfer(X86OS_DEVICE_CONTROL_DMA_WRITE, resource,
                                     offset, (void *)data, length);
}

int x86os_device_dma_read(x86os_device_resource_t resource, uint32_t offset,
                          void *data, uint32_t length) {
    return x86os_device_dma_transfer(X86OS_DEVICE_CONTROL_DMA_READ, resource,
                                     offset, data, length);
}

int x86os_device_dma_descriptor_set(x86os_device_resource_t dma,
                                    uint32_t descriptor_index,
                                    uint32_t buffer_offset,
                                    uint32_t length, uint32_t flags) {
    if (dma == 0U ||
        descriptor_index >= X86OS_DEVICE_DMA_DESCRIPTOR_CAPACITY ||
        buffer_offset < X86OS_DEVICE_DMA_DATA_OFFSET ||
        (buffer_offset &
         (X86OS_DEVICE_DMA_ADDRESS_ALIGNMENT - 1U)) != 0U ||
        length == 0U || (length & 3U) != 0U ||
        (flags & ~X86OS_DEVICE_DMA_DESCRIPTOR_INTERRUPT) != 0U) return -22;
    const x86os_device_dma_descriptor_t request = {
        .version = X86OS_DEVICE_ABI_VERSION,
        .struct_size = sizeof(request),
        .dma = dma,
        .descriptor_index = descriptor_index,
        .buffer_offset = buffer_offset,
        .length = length,
        .flags = flags,
    };
    return (int)x86os_syscall(X86OS_SYS_DEVICE_CONTROL,
        X86OS_DEVICE_CONTROL_DMA_DESCRIPTOR_SET, (uintptr_t)&request, 0U);
}

static int x86os_device_region_access_valid(
        x86os_device_resource_t region, uint32_t offset, uint32_t width) {
    return region != 0U &&
        (width == 1U || width == 2U || width == 4U) &&
        (offset & (width - 1U)) == 0U;
}

int x86os_device_region_read(x86os_device_resource_t region, uint32_t offset,
                             uint32_t width, uint32_t *value) {
    if (!x86os_device_region_access_valid(region, offset, width) ||
        value == NULL) return -22;
    const x86os_device_region_access_t request = {
        .version = X86OS_DEVICE_ABI_VERSION,
        .struct_size = sizeof(request),
        .region = region,
        .offset = offset,
        .width = width,
    };
    x86os_device_region_value_t result = {0};
    int status = (int)x86os_syscall(X86OS_SYS_DEVICE_CONTROL,
        X86OS_DEVICE_CONTROL_REGION_READ, (uintptr_t)&request,
        (uintptr_t)&result);
    if (status != 0) return status;
    if (result.version != X86OS_DEVICE_ABI_VERSION ||
        result.struct_size != sizeof(result) || result.region != region ||
        result.offset != offset || result.width != width ||
        result.reserved[0] != 0U || result.reserved[1] != 0U) return -84;
    *value = result.value;
    return 0;
}

int x86os_device_region_write(x86os_device_resource_t region, uint32_t offset,
                              uint32_t width, uint32_t value) {
    if (!x86os_device_region_access_valid(region, offset, width)) return -22;
    const x86os_device_region_access_t request = {
        .version = X86OS_DEVICE_ABI_VERSION,
        .struct_size = sizeof(request),
        .region = region,
        .offset = offset,
        .width = width,
        .value = value,
    };
    return (int)x86os_syscall(X86OS_SYS_DEVICE_CONTROL,
        X86OS_DEVICE_CONTROL_REGION_WRITE, (uintptr_t)&request, 0U);
}

int x86os_device_region_bind_dma(x86os_device_resource_t region,
                                 x86os_device_resource_t dma,
                                 uint32_t register_offset,
                                 uint32_t buffer_offset) {
    if (region == 0U || dma == 0U ||
        (buffer_offset & (X86OS_DEVICE_DMA_ADDRESS_ALIGNMENT - 1U)) != 0U)
        return -22;
    const x86os_device_region_dma_address_t request = {
        .version = X86OS_DEVICE_ABI_VERSION,
        .struct_size = sizeof(request),
        .region = region,
        .dma = dma,
        .register_offset = register_offset,
        .buffer_offset = buffer_offset,
    };
    return (int)x86os_syscall(X86OS_SYS_DEVICE_CONTROL,
        X86OS_DEVICE_CONTROL_REGION_BIND_DMA, (uintptr_t)&request, 0U);
}

int x86os_device_driver_bootstrap(
        x86os_device_driver_bootstrap_t *bootstrap) {
    if (bootstrap == NULL) return -22;
    return (int)x86os_syscall(X86OS_SYS_DEVICE_CONTROL,
        X86OS_DEVICE_CONTROL_DRIVER_BOOTSTRAP, 0U, (uintptr_t)bootstrap);
}

int x86os_device_driver_report(
        const x86os_device_driver_bootstrap_t *bootstrap,
        uint32_t report_type, uint32_t value) {
    if (bootstrap == NULL || bootstrap->version != X86OS_DEVICE_ABI_VERSION ||
        bootstrap->struct_size != sizeof(*bootstrap) ||
        bootstrap->session_generation == 0U ||
        bootstrap->session_epoch == 0U ||
        (report_type != X86OS_DEVICE_DRIVER_REPORT_SELF_TEST &&
         report_type != X86OS_DEVICE_DRIVER_REPORT_PROGRESS &&
         report_type != X86OS_DEVICE_DRIVER_REPORT_CHANNEL &&
         report_type != X86OS_DEVICE_DRIVER_REPORT_DIAGNOSTIC)) return -22;
    const x86os_device_driver_report_t report = {
        .version = X86OS_DEVICE_ABI_VERSION,
        .struct_size = sizeof(report),
        .session_slot = bootstrap->session_slot,
        .session_generation = bootstrap->session_generation,
        .session_epoch = bootstrap->session_epoch,
        .report_type = report_type,
        .value = value,
    };
    return (int)x86os_syscall(X86OS_SYS_DEVICE_CONTROL,
        X86OS_DEVICE_CONTROL_DRIVER_REPORT, (uintptr_t)&report, 0U);
}

int x86os_device_iommu_status(x86os_device_iommu_status_t *status) {
    if (status == NULL) return -22;
    return (int)x86os_syscall(X86OS_SYS_DEVICE_CONTROL,
        X86OS_DEVICE_CONTROL_IOMMU_STATUS, 0U, (uintptr_t)status);
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
