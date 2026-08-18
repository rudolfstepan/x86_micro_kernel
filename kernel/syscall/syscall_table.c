/**
 * @file syscall_table.c
 * @brief System call table and handler implementation
 * 
 * This module provides the kernel's system call interface, allowing
 * user programs to request kernel services via INT 0x80.
 */

#include <stdbool.h>
#include "arch/x86/include/interrupt.h"
#include "arch/x86/include/sys.h"
#include "drivers/video/display.h"
#include "drivers/video/framebuffer.h"
#include "drivers/char/kb.h"
#include "drivers/char/rtc.h"
#include "drivers/bus/drives.h"
#include "drivers/block/ata.h"
#include "drivers/block/fdd.h"
#include "drivers/block/block_device.h"
#include "drivers/block/partition.h"
#include "drivers/net/netdev.h"
#include "drivers/net/netstack.h"
#include "drivers/net/net_socket.h"
#include "drivers/net/tcp_socket.h"
#include "kernel/time/pit.h"
#include "kernel/sched/scheduler.h"
#include "kernel/proc/process.h"
#include "include/kernel/ipc.h"
#include "include/kernel/supervisor.h"
#include "include/kernel/storage_request_pool.h"
#include "include/kernel/storage_service.h"
#include "include/kernel/storage_maintenance.h"
#include "include/kernel/admin_maintenance.h"
#include "include/kernel/component_control.h"
#include "include/kernel/supervisor.h"
#include "arch/x86/mm/paging.h"
#include "fs/vfs/vfs.h"
#include "mm/kmalloc.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"  // For SYS_MALLOC, SYS_FREE, SYS_REALLOC, etc.
#include "lib/libc/string.h"

//---------------------------------------------------------------------------------------------
// Syscall Entry Points
//---------------------------------------------------------------------------------------------

/**
 * Simple kernel greeting function for syscall testing
 */
void kernel_hello(void) {
    printf("Hello from the kernel. All engines running.\n");
}

/**
 * Print a number from userspace for syscall testing
 */
void kernel_print_number(int number) {
    printf("Kernel received number: %d\n", number);
}

static uint32_t syscall_get_date(void) {
    int year, month, day;
    read_date(&year, &month, &day);
    return ((uint32_t)year << 16) | ((uint32_t)month << 8) | (uint32_t)day;
}

static uint32_t syscall_get_time(void) {
    int hours, minutes, seconds;
    read_time(&hours, &minutes, &seconds);
    return ((uint32_t)hours << 16) | ((uint32_t)minutes << 8) |
           (uint32_t)seconds;
}

static uint32_t syscall_memory_kb(void) {
    memory_stats_t stats;
    memory_get_stats(&stats);
    uint64_t kibibytes = stats.managed_bytes / 1024U;
    return kibibytes > UINT32_MAX ? UINT32_MAX : (uint32_t)kibibytes;
}

static int syscall_delay(const Registers *regs, uint32_t milliseconds) {
    if (milliseconds == 0) return 0;
    if (regs != NULL && (regs->cs & 3U) == 3U) {
        return scheduler_sleep_ms(milliseconds);
    }
    /* Early initialization and the rescue shell execute outside a scheduled
     * task and therefore cannot block on the scheduler. */
    pit_delay(milliseconds);
    return 0;
}

static int syscall_monotonic_ms(uint64_t *user_value) {
    uint64_t value = pit_monotonic_ms();
    return copy_to_user(user_value, &value, sizeof(value)) == 0 ? 0 : -14;
}

static int syscall_memory_stats(memory_stats_t *user_stats,
                                uint32_t user_size, uint32_t version) {
    size_t copy_size;
    if (version == MEMORY_STATS_V1_VERSION &&
        user_size >= MEMORY_STATS_V1_SIZE) {
        copy_size = MEMORY_STATS_V1_SIZE;
    } else if (version == MEMORY_STATS_VERSION &&
               user_size >= sizeof(memory_stats_t)) {
        copy_size = sizeof(memory_stats_t);
    } else {
        return -22;
    }
    memory_stats_t stats;
    memory_get_stats(&stats);
    stats.version = version;
    stats.struct_size = (uint32_t)copy_size;
    return copy_to_user(user_stats, &stats, copy_size) == 0 ? 0 : -14;
}

static int syscall_scheduler_stats(scheduler_resource_stats_t *user_stats,
                                   uint32_t user_size, uint32_t version) {
    if (version != SCHEDULER_RESOURCE_STATS_VERSION ||
        user_size < sizeof(scheduler_resource_stats_t)) return -22;
    scheduler_resource_stats_t stats;
    int result = scheduler_resource_stats(&stats);
    if (result != 0) return result;
    return copy_to_user(user_stats, &stats, sizeof(stats)) == 0 ? 0 : -14;
}

static int syscall_copy_from_user_space(void *destination,
                                        const void *user_source,
                                        size_t length) {
    return copy_from_user(destination, user_source, length);
}

static int syscall_ipc_create(ipc_handle_t *user_handle) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_handle;
    if (process == NULL ||
        !user_range_accessible(directory, address, sizeof(*user_handle),
                               true)) return -14;

    ipc_handle_t handle = IPC_INVALID_HANDLE;
    int result = ipc_create(process, &handle);
    if (result != 0) return result;
    if (copy_to_user_space(directory, address, &handle,
                           sizeof(handle)) != 0) {
        (void)ipc_close(process, handle);
        return -14;
    }
    return 0;
}

static int syscall_ipc_send(ipc_handle_t handle,
                            const ipc_message_t *user_message) {
    Process *process = scheduler_current_process();
    ipc_message_t message;
    if (process == NULL ||
        syscall_copy_from_user_space(&message, user_message,
                                     sizeof(message)) != 0) return -14;
    return ipc_send(process, handle, &message);
}

static int syscall_ipc_send_timeout(ipc_handle_t handle,
                                    const ipc_message_t *user_message,
                                    uint32_t timeout_ms) {
    Process *process = scheduler_current_process();
    ipc_message_t message;
    if (process == NULL ||
        syscall_copy_from_user_space(&message, user_message,
                                     sizeof(message)) != 0) return -14;
    return ipc_send_timeout(process, handle, &message, timeout_ms);
}

static int syscall_ipc_receive(ipc_handle_t handle,
                               ipc_message_t *user_message) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_message;
    ipc_message_t message;
    if (process == NULL ||
        !user_range_accessible(directory, address, sizeof(message), true) ||
        copy_from_user(&message, user_message, sizeof(message)) != 0) {
        return -14;
    }
    int result = ipc_receive(process, handle, &message);
    if (result != 0) return result;
    return copy_to_user_space(directory, address, &message,
                              sizeof(message)) == 0 ? 0 : -14;
}

static int syscall_ipc_receive_timeout(ipc_handle_t handle,
                                       ipc_message_t *user_message,
                                       uint32_t timeout_ms) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_message;
    ipc_message_t message;
    if (process == NULL ||
        !user_range_accessible(directory, address, sizeof(message), true) ||
        copy_from_user(&message, user_message, sizeof(message)) != 0) {
        return -14;
    }
    int result = ipc_receive_timeout(process, handle, &message, timeout_ms);
    if (result != 0) return result;
    return copy_to_user_space(directory, address, &message,
                              sizeof(message)) == 0 ? 0 : -14;
}

static int syscall_ipc_close(ipc_handle_t handle) {
    Process *process = scheduler_current_process();
    return process != NULL ? ipc_close(process, handle) : -1;
}

static int syscall_ipc_delegate(ipc_handle_t handle, int target_pid,
                                uint32_t rights) {
    Process *process = scheduler_current_process();
    return process != NULL
        ? process_ipc_delegate(process, handle, target_pid, rights) : -1;
}

static int syscall_reist_report(uint32_t report_type, uint32_t value) {
    Process *process = scheduler_current_process();
    if (process == NULL) return -13;
    return supervisor_probe_report(process->pid, process->generation,
                                   report_type, value, pit_monotonic_ms());
}

static int syscall_network_probe(void) {
    Process *process = scheduler_current_process();
    if (process == NULL) return -13;
    return supervisor_network_probe_request(process->pid, process->generation,
                                            pit_monotonic_ms());
}

static int syscall_network_probe_id(uint32_t *user_probe_id) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_probe_id;
    if (process == NULL ||
        !user_range_accessible(directory, address, sizeof(*user_probe_id), true))
        return -14;
    uint32_t probe_id = 0U;
    int result = supervisor_network_probe_request_id(
        process->pid, process->generation, pit_monotonic_ms(), &probe_id);
    if (result != 0) return result;
    return copy_to_user_space(directory, address, &probe_id,
                              sizeof(probe_id)) == 0 ? 0 : -14;
}

#define NETWORK_PROBE_STATS_VERSION 1U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t expired;
    uint32_t queue_fallback;
    uint32_t semantic_reject;
    uint32_t reserved;
} syscall_network_probe_stats_t;

_Static_assert(sizeof(syscall_network_probe_stats_t) == 24U,
               "network probe statistics ABI changed");

static int syscall_network_probe_stats(syscall_network_probe_stats_t *user_stats,
                                       uint32_t user_size,
                                       uint32_t version) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_stats;
    if (process == NULL ||
        !user_range_accessible(directory, address, sizeof(*user_stats), true))
        return -14;
    if (version != NETWORK_PROBE_STATS_VERSION ||
        user_size < sizeof(*user_stats)) return -22;
    supervisor_network_degradation_stats_t snapshot;
    int snapshot_result = supervisor_network_degradation_snapshot(&snapshot);
    if (snapshot_result != 0) return snapshot_result;
    syscall_network_probe_stats_t result = {
        .version = NETWORK_PROBE_STATS_VERSION,
        .struct_size = sizeof(result),
        .expired = snapshot.expired,
        .queue_fallback = snapshot.queue_fallback,
        .semantic_reject = snapshot.semantic_reject,
        .reserved = 0U,
    };
    return copy_to_user_space(directory, address, &result, sizeof(result)) == 0
        ? 0 : -14;
}

#define NETWORK_CONTROL_VERSION_V1 1U
#define NETWORK_CONTROL_VERSION 2U
#define NETWORK_CONTROL_STATUS 1U
#define NETWORK_CONTROL_CONFIGURE 2U
#define NETWORK_CONTROL_PING 3U
#define NETWORK_CONTROL_ARP_REQUEST 4U

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    uint32_t reserved;
    uint32_t ip_address;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t target_ip;
    uint32_t identifier;
    uint32_t sequence;
    uint32_t timeout_ms;
} network_control_request_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    int32_t operation_result;
    uint32_t available;
    uint32_t ready;
    uint32_t configured;
    uint32_t ip_address;
    uint32_t netmask;
    uint32_t gateway;
    char backend[16];
    uint8_t mac_address[6];
    uint8_t reserved[2];
    uint32_t dns_server;
} network_control_result_t;

_Static_assert(sizeof(network_control_request_t) == 44U,
               "network control request ABI changed");
_Static_assert(sizeof(network_control_result_t) == 64U,
               "network control result ABI changed");

static int syscall_network_control(
        const network_control_request_t *user_request,
        network_control_result_t *user_result) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t request_address = (uint32_t)(uintptr_t)user_request;
    uint32_t result_address = (uint32_t)(uintptr_t)user_result;
    if (process == NULL ||
        !user_range_accessible(directory, request_address,
                               sizeof(network_control_request_t), false)) {
        return -14;
    }

    network_control_request_t request;
    if (copy_from_user(&request, user_request, sizeof(request)) != 0 ||
        (request.version != NETWORK_CONTROL_VERSION_V1 &&
         request.version != NETWORK_CONTROL_VERSION) ||
        request.struct_size != sizeof(request) || request.reserved != 0U) {
        return -22;
    }
    size_t result_size = request.version == NETWORK_CONTROL_VERSION_V1
        ? 60U : sizeof(network_control_result_t);
    if (!user_range_accessible(directory, result_address, result_size, true))
        return -14;

    network_control_result_t result = {0};
    result.version = request.version;
    result.struct_size = (uint32_t)result_size;
    result.available = netdev_available() ? 1U : 0U;
    result.ready = netdev_component_ready() ? 1U : 0U;
    result.configured = netstack_is_configured() ? 1U : 0U;
    result.ip_address = netstack_get_ip_address();
    result.netmask = netstack_get_netmask();
    result.gateway = netstack_get_gateway();
    result.dns_server = netstack_get_dns_server();
    (void)netdev_get_mac_address(result.mac_address);
    const char *backend = netdev_backend_name();
    size_t backend_index = 0U;
    while (backend[backend_index] != '\0' &&
           backend_index + 1U < sizeof(result.backend)) {
        result.backend[backend_index] = backend[backend_index];
        ++backend_index;
    }
    result.backend[backend_index] = '\0';

    int operation_result = 0;
    switch (request.operation) {
        case NETWORK_CONTROL_STATUS:
            break;
        case NETWORK_CONTROL_CONFIGURE:
            if (request.ip_address == 0U || request.netmask == 0U ||
                request.gateway == 0U) {
                operation_result = -22;
            } else if (!netstack_set_config(request.ip_address,
                                             request.netmask,
                                             request.gateway)) {
                operation_result = -5;
            }
            break;
        case NETWORK_CONTROL_PING:
            if (request.target_ip == 0U || request.timeout_ms == 0U ||
                request.timeout_ms > 5000U) {
                operation_result = -22;
            } else if (!netdev_available() || !netstack_is_configured()) {
                operation_result = -19;
            } else if (!netstack_ping(request.target_ip,
                                      (uint16_t)request.identifier,
                                      (uint16_t)request.sequence,
                                      request.timeout_ms)) {
                operation_result = -110;
            }
            break;
        case NETWORK_CONTROL_ARP_REQUEST:
            if (request.target_ip == 0U ||
                !supervisor_network_request_arp_resolution(
                    request.target_ip)) {
                operation_result = -11;
            }
            break;
        default:
            operation_result = -22;
            break;
    }
    result.operation_result = operation_result;
    result.ip_address = netstack_get_ip_address();
    result.netmask = netstack_get_netmask();
    result.configured = netstack_is_configured() ? 1U : 0U;
    result.gateway = netstack_get_gateway();
    if (copy_to_user(user_result, &result, result_size) != 0) return -14;
    return operation_result;
}

enum {
    UDP_SOCKET_CONTROL_OPEN = 1U,
    UDP_SOCKET_CONTROL_BIND = 2U,
    UDP_SOCKET_CONTROL_CLOSE = 3U,
    UDP_SOCKET_CONTROL_STATS = 4U,
};
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    net_socket_handle_t socket;
    uint16_t port;
    uint16_t reserved;
    uint32_t active_sockets;
    uint32_t queued_datagrams;
    uint32_t dropped_datagrams;
} syscall_udp_socket_control_t;
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    net_socket_handle_t socket;
    uint32_t ip;
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t length;
    uint32_t timeout_ms;
} syscall_udp_datagram_t;

_Static_assert(sizeof(syscall_udp_socket_control_t) == 32U,
               "UDP socket control ABI changed");
_Static_assert(sizeof(syscall_udp_datagram_t) == 28U,
               "UDP datagram ABI changed");

static int syscall_udp_socket_control(syscall_udp_socket_control_t *user) {
    Process *process = scheduler_current_process();
    if (process == NULL || user == NULL) return -14;
    uint32_t address = (uint32_t)(uintptr_t)user;
    page_directory_t *directory = paging_current_directory();
    if (!user_range_accessible(directory, address, sizeof(*user), true))
        return -14;
    syscall_udp_socket_control_t control;
    if (copy_from_user(&control, user, sizeof(control)) != 0) return -14;
    if (control.version != NET_SOCKET_ABI_VERSION ||
        control.struct_size != sizeof(control) || control.reserved != 0U ||
        control.active_sockets != 0U || control.queued_datagrams != 0U ||
        control.dropped_datagrams != 0U) return -22;
    int result = -22;
    net_socket_handle_t opened = 0U;
    if (control.operation == UDP_SOCKET_CONTROL_OPEN && control.socket == 0U &&
        control.port == 0U) {
        result = net_socket_open(process->pid, process->generation, &opened);
        if (result == 0) {
            int descriptor = process_descriptor_install(
                process, PROCESS_DESCRIPTOR_UDP_SOCKET, opened);
            if (descriptor < 0) {
                (void)net_socket_close(process->pid, process->generation,
                                       opened);
                result = descriptor;
            } else control.socket = (uint32_t)descriptor;
        }
    } else if (control.operation == UDP_SOCKET_CONTROL_BIND &&
               control.port != 0U) {
        uint32_t handle = 0U;
        result = process_descriptor_resolve(
            process, (int)control.socket, PROCESS_DESCRIPTOR_UDP_SOCKET,
            &handle);
        if (result == 0)
        result = net_socket_bind(process->pid, process->generation,
                                 handle, control.port);
    } else if (control.operation == UDP_SOCKET_CONTROL_CLOSE &&
               control.port == 0U) {
        uint32_t handle = 0U;
        result = process_descriptor_resolve(
            process, (int)control.socket, PROCESS_DESCRIPTOR_UDP_SOCKET,
            &handle);
        if (result == 0) result = process_file_close(
            process, (int)control.socket);
    } else if (control.operation == UDP_SOCKET_CONTROL_STATS &&
               control.socket == 0U && control.port == 0U) {
        net_socket_stats_t stats;
        net_socket_get_stats(&stats);
        control.active_sockets = stats.active_sockets;
        control.queued_datagrams = stats.queued_datagrams;
        control.dropped_datagrams = stats.dropped_datagrams;
        result = 0;
    }
    if (result == 0 && copy_to_user(user, &control, sizeof(control)) != 0) {
        if (opened != 0U && control.socket != 0U)
            (void)process_file_close(process, (int)control.socket);
        return -14;
    }
    return result;
}

static int syscall_udp_socket_sendto(
        const syscall_udp_datagram_t *user_datagram,
        const uint8_t *user_data) {
    Process *process = scheduler_current_process();
    if (process == NULL || user_datagram == NULL) return -14;
    page_directory_t *directory = paging_current_directory();
    uint32_t datagram_address = (uint32_t)(uintptr_t)user_datagram;
    if (!user_range_accessible(directory, datagram_address,
                               sizeof(*user_datagram), false)) return -14;
    syscall_udp_datagram_t datagram;
    if (copy_from_user(&datagram, user_datagram, sizeof(datagram)) != 0)
        return -14;
    if (datagram.version != NET_SOCKET_ABI_VERSION ||
        datagram.struct_size != sizeof(datagram) ||
        datagram.source_port != 0U || datagram.destination_port == 0U ||
        datagram.timeout_ms > 10000U ||
        datagram.length > NET_SOCKET_MAX_DATAGRAM)
        return -22;
    uint8_t data[NET_SOCKET_MAX_DATAGRAM];
    if (datagram.length != 0U) {
        uint32_t data_address = (uint32_t)(uintptr_t)user_data;
        if (!user_range_accessible(directory, data_address, datagram.length,
                                   false) ||
            copy_from_user(data, user_data, datagram.length) != 0) return -14;
    }
    uint32_t handle = 0U;
    int descriptor_result = process_descriptor_resolve(
        process, (int)datagram.socket, PROCESS_DESCRIPTOR_UDP_SOCKET, &handle);
    if (descriptor_result != 0) return descriptor_result;
    return net_socket_sendto(process->pid, process->generation,
                             handle, datagram.ip,
                             datagram.destination_port, data,
                             datagram.length, datagram.timeout_ms);
}

static int syscall_udp_socket_recvfrom(syscall_udp_datagram_t *user_datagram,
                                       uint8_t *user_data) {
    Process *process = scheduler_current_process();
    if (process == NULL || user_datagram == NULL) return -14;
    page_directory_t *directory = paging_current_directory();
    uint32_t datagram_address = (uint32_t)(uintptr_t)user_datagram;
    if (!user_range_accessible(directory, datagram_address,
                               sizeof(*user_datagram), true)) return -14;
    syscall_udp_datagram_t datagram;
    if (copy_from_user(&datagram, user_datagram, sizeof(datagram)) != 0)
        return -14;
    uint32_t capacity = datagram.length;
    if (datagram.version != NET_SOCKET_ABI_VERSION ||
        datagram.struct_size != sizeof(datagram) || datagram.ip != 0U ||
        datagram.source_port != 0U || datagram.destination_port != 0U ||
        capacity > NET_SOCKET_MAX_DATAGRAM)
        return -22;
    uint32_t data_address = (uint32_t)(uintptr_t)user_data;
    if (capacity != 0U &&
        !user_range_accessible(directory, data_address, capacity, true))
        return -14;
    uint8_t data[NET_SOCKET_MAX_DATAGRAM];
    net_socket_datagram_t received;
    uint32_t handle = 0U;
    int descriptor_result = process_descriptor_resolve(
        process, (int)datagram.socket, PROCESS_DESCRIPTOR_UDP_SOCKET, &handle);
    if (descriptor_result != 0) return descriptor_result;
    int result = net_socket_recvfrom(process->pid, process->generation,
                                     handle, &received, data,
                                     capacity, datagram.timeout_ms);
    if (result < 0) return result;
    datagram.ip = received.source_ip;
    datagram.source_port = received.source_port;
    datagram.destination_port = received.destination_port;
    datagram.length = received.length;
    if (received.length != 0U &&
        copy_to_user(user_data, data, received.length) != 0) return -14;
    return copy_to_user(user_datagram, &datagram, sizeof(datagram)) == 0
        ? result : -14;
}

static int syscall_udp_socket_ingress(
        const syscall_udp_datagram_t *user_datagram,
        const uint8_t *user_data) {
    Process *process = scheduler_current_process();
    if (process == NULL ||
        process->domain_profile.kind != PROCESS_DOMAIN_PROBE ||
        user_datagram == NULL) return -13;
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_datagram;
    if (!user_range_accessible(directory, address, sizeof(*user_datagram),
                               false)) return -14;
    syscall_udp_datagram_t datagram;
    if (copy_from_user(&datagram, user_datagram, sizeof(datagram)) != 0)
        return -14;
    if (datagram.version != NET_SOCKET_ABI_VERSION ||
        datagram.struct_size != sizeof(datagram) || datagram.socket != 0U ||
        datagram.ip == 0U || datagram.source_port == 0U ||
        datagram.destination_port == 0U || datagram.timeout_ms != 0U ||
        datagram.length > NET_SOCKET_MAX_DATAGRAM) return -22;
    uint8_t data[NET_SOCKET_MAX_DATAGRAM];
    if (datagram.length != 0U) {
        uint32_t data_address = (uint32_t)(uintptr_t)user_data;
        if (!user_range_accessible(directory, data_address, datagram.length,
                                   false) ||
            copy_from_user(data, user_data, datagram.length) != 0) return -14;
    }
    return net_socket_ingress(datagram.ip, datagram.source_port,
                              datagram.destination_port, data,
                              datagram.length);
}

enum {
    TCP_SOCKET_CONTROL_OPEN = 1U,
    TCP_SOCKET_CONTROL_CLOSE = 2U,
    TCP_SOCKET_CONTROL_STATS = 3U,
};
typedef struct {
    uint32_t version, struct_size, operation;
    tcp_socket_handle_t socket;
    uint32_t timeout_ms, active_sockets, established_sockets, retransmissions;
} syscall_tcp_socket_control_t;
typedef struct {
    uint32_t version, struct_size;
    tcp_socket_handle_t socket;
    uint32_t destination_ip;
    uint16_t destination_port, reserved;
    uint32_t timeout_ms;
} syscall_tcp_connect_t;
typedef struct {
    uint32_t version, struct_size;
    tcp_socket_handle_t socket;
    uint32_t length, timeout_ms;
} syscall_tcp_io_t;
typedef struct {
    uint32_t version, struct_size;
    tcp_socket_handle_t socket;
    uint16_t port, backlog;
    uint32_t reserved;
} syscall_tcp_listen_t;
/* Accept is in/out: the caller supplies only listener and timeout; the kernel
 * publishes the new descriptor and peer tuple after all validation succeeds. */
typedef struct {
    uint32_t version, struct_size;
    tcp_socket_handle_t listener, socket;
    uint32_t peer_ip;
    uint16_t peer_port, reserved;
    uint32_t timeout_ms;
} syscall_tcp_accept_t;

_Static_assert(sizeof(syscall_tcp_socket_control_t) == 32U,
               "TCP control ABI changed");
_Static_assert(sizeof(syscall_tcp_connect_t) == 24U,
               "TCP connect ABI changed");
_Static_assert(sizeof(syscall_tcp_io_t) == 20U, "TCP I/O ABI changed");
_Static_assert(sizeof(syscall_tcp_listen_t) == 20U,
               "TCP listen ABI changed");
_Static_assert(sizeof(syscall_tcp_accept_t) == 28U,
               "TCP accept ABI changed");
_Static_assert(sizeof(tcp_socket_segment_t) == 36U,
               "TCP ingress ABI changed");

static int syscall_tcp_socket_control(syscall_tcp_socket_control_t *user) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    if (process == NULL || user == NULL || !user_range_accessible(
            directory, (uint32_t)(uintptr_t)user, sizeof(*user), true))
        return -14;
    syscall_tcp_socket_control_t control;
    if (copy_from_user(&control, user, sizeof(control)) != 0) return -14;
    if (control.version != TCP_SOCKET_ABI_VERSION ||
        control.struct_size != sizeof(control) || control.active_sockets != 0U ||
        control.established_sockets != 0U || control.retransmissions != 0U)
        return -22;
    int result = -22; tcp_socket_handle_t opened = 0U;
    if (control.operation == TCP_SOCKET_CONTROL_OPEN && control.socket == 0U &&
        control.timeout_ms == 0U) {
        result = tcp_socket_open(process->pid, process->generation, &opened);
        if (result == 0) {
            int descriptor = process_descriptor_install(
                process, PROCESS_DESCRIPTOR_TCP_SOCKET, opened);
            if (descriptor < 0) {
                (void)tcp_socket_close(process->pid, process->generation,
                                       opened, 0U);
                result = descriptor;
            } else control.socket = (uint32_t)descriptor;
        }
    } else if (control.operation == TCP_SOCKET_CONTROL_CLOSE &&
               control.socket != 0U) {
        uint32_t handle = 0U;
        result = process_descriptor_resolve(
            process, (int)control.socket, PROCESS_DESCRIPTOR_TCP_SOCKET,
            &handle);
        if (result == 0) result = tcp_socket_close(
            process->pid, process->generation, handle, control.timeout_ms);
        if (result == 0) result = process_descriptor_release(
            process, (int)control.socket, PROCESS_DESCRIPTOR_TCP_SOCKET);
    } else if (control.operation == TCP_SOCKET_CONTROL_STATS &&
               control.socket == 0U && control.timeout_ms == 0U) {
        tcp_socket_stats_t stats; tcp_socket_get_stats(&stats);
        control.active_sockets = stats.active_sockets;
        control.established_sockets = stats.established_sockets;
        control.retransmissions = stats.retransmissions; result = 0;
    }
    if (result == 0 && copy_to_user(user, &control, sizeof(control)) != 0) {
        if (opened != 0U && control.socket != 0U)
            (void)process_file_close(process, (int)control.socket);
        return -14;
    }
    return result;
}

static int syscall_tcp_socket_connect(const syscall_tcp_connect_t *user) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    if (process == NULL || user == NULL || !user_range_accessible(
            directory, (uint32_t)(uintptr_t)user, sizeof(*user), false))
        return -14;
    syscall_tcp_connect_t request;
    if (copy_from_user(&request, user, sizeof(request)) != 0) return -14;
    if (request.version != TCP_SOCKET_ABI_VERSION ||
        request.struct_size != sizeof(request) || request.reserved != 0U)
        return -22;
    uint32_t handle = 0U;
    int descriptor_result = process_descriptor_resolve(
        process, (int)request.socket, PROCESS_DESCRIPTOR_TCP_SOCKET, &handle);
    if (descriptor_result != 0) return descriptor_result;
    return tcp_socket_connect(process->pid, process->generation,
                              handle, request.destination_ip,
                              request.destination_port, request.timeout_ms);
}

static int syscall_tcp_socket_listen(const syscall_tcp_listen_t *user) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    if (process == NULL || user == NULL || !user_range_accessible(
            directory, (uint32_t)(uintptr_t)user, sizeof(*user), false))
        return -14;
    syscall_tcp_listen_t request;
    if (copy_from_user(&request, user, sizeof(request)) != 0) return -14;
    if (request.version != TCP_SOCKET_ABI_VERSION ||
        request.struct_size != sizeof(request) || request.reserved != 0U)
        return -22;
    uint32_t handle = 0U;
    int result = process_descriptor_resolve(
        process, (int)request.socket, PROCESS_DESCRIPTOR_TCP_SOCKET, &handle);
    if (result != 0) return result;
    return tcp_socket_listen(process->pid, process->generation, handle,
                             request.port, request.backlog);
}

/* Resolve the listener capability before entering the bounded kernel wait.
 * A child is installed atomically as a process descriptor; failed copy-out
 * closes that descriptor so no accepted socket leaks authority. */
static int syscall_tcp_socket_accept(syscall_tcp_accept_t *user) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    if (process == NULL || user == NULL || !user_range_accessible(
            directory, (uint32_t)(uintptr_t)user, sizeof(*user), true))
        return -14;
    syscall_tcp_accept_t request;
    if (copy_from_user(&request, user, sizeof(request)) != 0) return -14;
    if (request.version != TCP_SOCKET_ABI_VERSION ||
        request.struct_size != sizeof(request) || request.listener == 0U ||
        request.socket != 0U || request.peer_ip != 0U ||
        request.peer_port != 0U || request.reserved != 0U ||
        request.timeout_ms > TCP_SOCKET_MAX_TIMEOUT_MS) return -22;
    uint32_t listener = 0U;
    int result = process_descriptor_resolve(
        process, (int)request.listener, PROCESS_DESCRIPTOR_TCP_SOCKET,
        &listener);
    if (result != 0) return result;
    tcp_socket_handle_t accepted = 0U;
    result = tcp_socket_accept(process->pid, process->generation, listener,
                               &accepted, &request.peer_ip,
                               &request.peer_port, request.timeout_ms);
    if (result != 0) return result;
    int descriptor = process_descriptor_install(
        process, PROCESS_DESCRIPTOR_TCP_SOCKET, accepted);
    if (descriptor < 0) {
        (void)tcp_socket_close(process->pid, process->generation,
                               accepted, 0U);
        return descriptor;
    }
    request.socket = (uint32_t)descriptor;
    if (copy_to_user(user, &request, sizeof(request)) != 0) {
        (void)process_file_close(process, descriptor);
        return -14;
    }
    return 0;
}

static int syscall_tcp_socket_send(const syscall_tcp_io_t *user,
                                   const uint8_t *user_data) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    if (process == NULL || user == NULL || !user_range_accessible(
            directory, (uint32_t)(uintptr_t)user, sizeof(*user), false))
        return -14;
    syscall_tcp_io_t request;
    if (copy_from_user(&request, user, sizeof(request)) != 0) return -14;
    if (request.version != TCP_SOCKET_ABI_VERSION ||
        request.struct_size != sizeof(request) || request.length == 0U ||
        request.length > TCP_SOCKET_MAX_SEGMENT) return -22;
    uint8_t data[TCP_SOCKET_MAX_SEGMENT];
    if (!user_range_accessible(directory, (uint32_t)(uintptr_t)user_data,
                               request.length, false) ||
        copy_from_user(data, user_data, request.length) != 0) return -14;
    uint32_t handle = 0U;
    int descriptor_result = process_descriptor_resolve(
        process, (int)request.socket, PROCESS_DESCRIPTOR_TCP_SOCKET, &handle);
    if (descriptor_result != 0) return descriptor_result;
    return tcp_socket_send(process->pid, process->generation, handle,
                           data, request.length, request.timeout_ms);
}

static int syscall_tcp_socket_receive(syscall_tcp_io_t *user,
                                      uint8_t *user_data) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    if (process == NULL || user == NULL || !user_range_accessible(
            directory, (uint32_t)(uintptr_t)user, sizeof(*user), true))
        return -14;
    syscall_tcp_io_t request;
    if (copy_from_user(&request, user, sizeof(request)) != 0) return -14;
    if (request.version != TCP_SOCKET_ABI_VERSION ||
        request.struct_size != sizeof(request) || request.length == 0U ||
        request.length > TCP_SOCKET_RECEIVE_CAPACITY) return -22;
    if (!user_range_accessible(directory, (uint32_t)(uintptr_t)user_data,
                               request.length, true)) return -14;
    uint8_t data[TCP_SOCKET_RECEIVE_CAPACITY];
    uint32_t handle = 0U;
    int descriptor_result = process_descriptor_resolve(
        process, (int)request.socket, PROCESS_DESCRIPTOR_TCP_SOCKET, &handle);
    if (descriptor_result != 0) return descriptor_result;
    int result = tcp_socket_receive(process->pid, process->generation,
                                    handle, data, request.length,
                                    request.timeout_ms);
    if (result < 0) return result;
    request.length = (uint32_t)result;
    if (result != 0 && copy_to_user(user_data, data, (size_t)result) != 0)
        return -14;
    return copy_to_user(user, &request, sizeof(request)) == 0 ? result : -14;
}

static int syscall_tcp_socket_ingress(const tcp_socket_segment_t *user_segment,
                                      const uint8_t *user_data) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    if (process == NULL ||
        process->domain_profile.kind != PROCESS_DOMAIN_PROBE) return -13;
    if (user_segment == NULL || !user_range_accessible(
            directory, (uint32_t)(uintptr_t)user_segment,
            sizeof(*user_segment), false)) return -14;
    tcp_socket_segment_t segment;
    if (copy_from_user(&segment, user_segment, sizeof(segment)) != 0)
        return -14;
    if (segment.length > TCP_SOCKET_MAX_SEGMENT) return -22;
    uint8_t data[TCP_SOCKET_MAX_SEGMENT];
    if (segment.length != 0U && (!user_range_accessible(
            directory, (uint32_t)(uintptr_t)user_data, segment.length, false) ||
        copy_from_user(data, user_data, segment.length) != 0)) return -14;
    return tcp_socket_ingress(&segment, data);
}

_Static_assert(sizeof(supervisor_arp_binding_t) == 24U,
               "REIST ARP binding ABI changed");

static int syscall_reist_arp_binding(
        const supervisor_arp_binding_t *user_binding) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_binding;
    if (process == NULL ||
        !user_range_accessible(directory, address, sizeof(*user_binding), false))
        return -14;
    supervisor_arp_binding_t binding;
    if (copy_from_user(&binding, user_binding, sizeof(binding)) != 0) return -14;
    if (binding.version != SUPERVISOR_ARP_BINDING_VERSION ||
        binding.struct_size < sizeof(binding)) return -22;
    return supervisor_network_commit_arp_binding(
        process->pid, process->generation, &binding);
}

_Static_assert(sizeof(supervisor_arp_reply_t) == 24U,
               "REIST ARP reply ABI changed");

static int syscall_reist_arp_reply(
        const supervisor_arp_reply_t *user_reply) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_reply;
    if (process == NULL ||
        !user_range_accessible(directory, address, sizeof(*user_reply), false))
        return -14;
    supervisor_arp_reply_t reply;
    if (copy_from_user(&reply, user_reply, sizeof(reply)) != 0) return -14;
    if (reply.version != SUPERVISOR_ARP_REPLY_VERSION ||
        reply.struct_size < sizeof(reply)) return -22;
    return supervisor_network_send_arp_reply(
        process->pid, process->generation, &reply);
}

_Static_assert(sizeof(supervisor_arp_resolution_t) == 16U,
               "REIST ARP resolution ABI changed");

static int syscall_reist_arp_resolution(
        const supervisor_arp_resolution_t *user_request) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_request;
    if (process == NULL ||
        !user_range_accessible(directory, address, sizeof(*user_request), false))
        return -14;
    supervisor_arp_resolution_t request;
    if (copy_from_user(&request, user_request, sizeof(request)) != 0) return -14;
    if (request.version != SUPERVISOR_ARP_RESOLUTION_VERSION ||
        request.struct_size < sizeof(request)) return -22;
    return supervisor_network_send_arp_request(
        process->pid, process->generation, &request);
}

static int syscall_network_arp_resolve(uint32_t target_ip) {
    return supervisor_network_request_arp_resolution(target_ip) ? 0 : -11;
}

_Static_assert(sizeof(supervisor_icmp_echo_reply_t) == 16U,
               "REIST ICMP echo reply ABI changed");

static int syscall_reist_icmp_echo_reply(
        const supervisor_icmp_echo_reply_t *user_reply) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_reply;
    if (process == NULL ||
        !user_range_accessible(directory, address, sizeof(*user_reply), false))
        return -14;
    supervisor_icmp_echo_reply_t reply;
    if (copy_from_user(&reply, user_reply, sizeof(reply)) != 0) return -14;
    if (reply.version != SUPERVISOR_ICMP_ECHO_REPLY_VERSION ||
        reply.struct_size < sizeof(reply)) return -22;
    return supervisor_network_send_icmp_echo_reply(
        process->pid, process->generation, &reply);
}

static int syscall_reist_dhcp_commit(
        const supervisor_dhcp_commit_t *user_commit) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_commit;
    if (process == NULL ||
        !user_range_accessible(directory, address, sizeof(*user_commit), false))
        return -14;
    supervisor_dhcp_commit_t commit;
    if (copy_from_user(&commit, user_commit, sizeof(commit)) != 0) return -14;
    if (commit.version != SUPERVISOR_DHCP_COMMIT_VERSION ||
        commit.struct_size < sizeof(commit)) return -22;
    return supervisor_network_commit_dhcp_config(
        process->pid, process->generation, &commit);
}

_Static_assert(sizeof(supervisor_dhcp_renew_request_t) == 16U,
               "REIST DHCP renew ABI changed");

static int syscall_reist_dhcp_renew(
        const supervisor_dhcp_renew_request_t *user_request) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_request;
    if (process == NULL ||
        !user_range_accessible(directory, address, sizeof(*user_request),
                               false)) return -14;
    supervisor_dhcp_renew_request_t request;
    if (copy_from_user(&request, user_request, sizeof(request)) != 0)
        return -14;
    if (request.version != SUPERVISOR_DHCP_RENEW_REQUEST_VERSION ||
        request.struct_size < sizeof(request)) return -22;
    return supervisor_network_request_dhcp_renewal(
        process->pid, process->generation, &request);
}

_Static_assert(sizeof(supervisor_network_frame_t) == 1536U,
               "REIST network frame ABI changed");

static int syscall_reist_network_frame(
        supervisor_network_frame_t *user_frame) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_frame;
    if (process == NULL ||
        !user_range_accessible(directory, address, sizeof(*user_frame), true))
        return -14;
    supervisor_network_frame_t frame;
    int result = supervisor_network_receive_frame(
        process->pid, process->generation, &frame);
    if (result != 0) return result;
    if (copy_to_user(user_frame, &frame, sizeof(frame)) != 0) return -14;
    return supervisor_network_confirm_frame_delivery(
        process->pid, process->generation, &frame);
}

static int syscall_reist_udp_echo_reply(
        const supervisor_udp_echo_reply_t *user_reply) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_reply;
    if (process == NULL ||
        !user_range_accessible(directory, address, sizeof(*user_reply), false))
        return -14;
    supervisor_udp_echo_reply_t reply;
    if (copy_from_user(&reply, user_reply, sizeof(reply)) != 0) return -14;
    if (reply.version != SUPERVISOR_UDP_ECHO_REPLY_VERSION ||
        reply.struct_size < sizeof(reply)) return -22;
    return supervisor_network_send_udp_echo_reply(
        process->pid, process->generation, &reply);
}

_Static_assert(sizeof(supervisor_udp_bind_request_t) == 16U,
               "REIST UDP bind request ABI changed");
_Static_assert(sizeof(supervisor_udp_reply_t) == 16U,
               "REIST UDP reply ABI changed");
_Static_assert(sizeof(supervisor_udp_ingress_t) == 40U,
               "REIST UDP ingress ABI changed");

static int syscall_reist_udp_bind(
        const supervisor_udp_bind_request_t *user_request,
        supervisor_udp_binding_handle_t *user_handle) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t request_address = (uint32_t)(uintptr_t)user_request;
    uint32_t handle_address = (uint32_t)(uintptr_t)user_handle;
    if (process == NULL ||
        !user_range_accessible(directory, request_address,
                               sizeof(*user_request), false) ||
        !user_range_accessible(directory, handle_address,
                               sizeof(*user_handle), true)) return -14;
    supervisor_udp_bind_request_t request;
    if (copy_from_user(&request, user_request, sizeof(request)) != 0)
        return -14;
    supervisor_udp_binding_handle_t handle = 0U;
    int result = supervisor_network_udp_bind(
        process->pid, process->generation, &request, &handle);
    if (result != 0) return result;
    if (copy_to_user(user_handle, &handle, sizeof(handle)) != 0) {
        (void)supervisor_network_udp_unbind(
            process->pid, process->generation, handle);
        return -14;
    }
    return 0;
}

static int syscall_reist_udp_unbind(supervisor_udp_binding_handle_t handle) {
    Process *process = scheduler_current_process();
    return process == NULL ? -13 : supervisor_network_udp_unbind(
        process->pid, process->generation, handle);
}

static int syscall_reist_udp_reply(
        const supervisor_udp_reply_t *user_reply) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_reply;
    if (process == NULL ||
        !user_range_accessible(directory, address, sizeof(*user_reply), false))
        return -14;
    supervisor_udp_reply_t reply;
    if (copy_from_user(&reply, user_reply, sizeof(reply)) != 0) return -14;
    return supervisor_network_send_udp_reply(
        process->pid, process->generation, &reply);
}

static int syscall_reist_udp_ingress(
        supervisor_udp_ingress_t *user_ingress, const uint8_t *user_data) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t ingress_address = (uint32_t)(uintptr_t)user_ingress;
    if (process == NULL ||
        !user_range_accessible(directory, ingress_address,
                               sizeof(*user_ingress), false) ||
        !user_range_accessible(directory, ingress_address,
                               sizeof(*user_ingress), true)) return -14;

    supervisor_udp_ingress_t ingress;
    if (copy_from_user(&ingress, user_ingress, sizeof(ingress)) != 0)
        return -14;
    if (ingress.version != SUPERVISOR_UDP_INGRESS_VERSION ||
        ingress.struct_size != sizeof(ingress) ||
        ingress.request_id != 0U ||
        ingress.data_length > SUPERVISOR_UDP_ECHO_MAX_DATA) return -22;

    uint8_t data[SUPERVISOR_UDP_ECHO_MAX_DATA] = {0};
    const uint8_t *data_argument = NULL;
    if (ingress.data_length != 0U) {
        uint32_t data_address = (uint32_t)(uintptr_t)user_data;
        if (!user_range_accessible(directory, data_address,
                                   ingress.data_length, false) ||
            copy_from_user(data, user_data, ingress.data_length) != 0)
            return -14;
        data_argument = data;
    }

    uint32_t request_id = 0U;
    int result = supervisor_network_udp_ingress(
        process->pid, process->generation, &ingress, data_argument,
        &request_id);
    if (result != 0) return result;
    ingress.request_id = request_id;
    if (copy_to_user(user_ingress, &ingress, sizeof(ingress)) != 0) {
        if (request_id != 0U)
            (void)supervisor_network_cancel_udp_ingress(
                process->pid, process->generation, ingress.binding,
                request_id);
        return -14;
    }
    return 0;
}

_Static_assert(sizeof(supervisor_icmp_ingress_t) == 40U,
               "REIST ICMP ingress ABI changed");

static int syscall_reist_icmp_ingress(
        const supervisor_icmp_ingress_t *user_ingress,
        const uint8_t *user_data) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t ingress_address = (uint32_t)(uintptr_t)user_ingress;
    if (process == NULL ||
        !user_range_accessible(directory, ingress_address,
                               sizeof(*user_ingress), false)) return -14;
    supervisor_icmp_ingress_t ingress;
    if (copy_from_user(&ingress, user_ingress, sizeof(ingress)) != 0)
        return -14;
    if (ingress.version != SUPERVISOR_ICMP_INGRESS_VERSION ||
        ingress.struct_size != sizeof(ingress) ||
        ingress.data_length > SUPERVISOR_ICMP_ECHO_MAX_DATA) return -22;
    uint8_t data[SUPERVISOR_ICMP_ECHO_MAX_DATA] = {0};
    const uint8_t *data_argument = NULL;
    if (ingress.data_length != 0U) {
        uint32_t data_address = (uint32_t)(uintptr_t)user_data;
        if (!user_range_accessible(directory, data_address,
                                   ingress.data_length, false) ||
            copy_from_user(data, user_data, ingress.data_length) != 0)
            return -14;
        data_argument = data;
    }
    return supervisor_network_icmp_ingress(
        process->pid, process->generation, &ingress, data_argument);
}

_Static_assert(sizeof(supervisor_dhcp_ingress_t) == 52U,
               "REIST DHCP ingress ABI changed");

static int syscall_reist_dhcp_ingress(
        const supervisor_dhcp_ingress_t *user_ingress) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_ingress;
    if (process == NULL ||
        !user_range_accessible(directory, address, sizeof(*user_ingress),
                               false)) return -14;
    supervisor_dhcp_ingress_t ingress;
    if (copy_from_user(&ingress, user_ingress, sizeof(ingress)) != 0)
        return -14;
    return supervisor_network_dhcp_ingress(
        process->pid, process->generation, &ingress);
}

_Static_assert(sizeof(supervisor_dhcp_boot_start_t) == 8U,
               "REIST DHCP boot start ABI changed");

static int syscall_reist_dhcp_boot_start(
        const supervisor_dhcp_boot_start_t *user_request) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_request;
    if (process == NULL ||
        !user_range_accessible(directory, address, sizeof(*user_request),
                               false)) return -14;
    supervisor_dhcp_boot_start_t request;
    if (copy_from_user(&request, user_request, sizeof(request)) != 0)
        return -14;
    return supervisor_network_start_dhcp_boot(
        process->pid, process->generation, &request);
}

_Static_assert(sizeof(storage_request_submit_t) == 28U,
               "storage submit ABI changed");
_Static_assert(sizeof(storage_request_descriptor_t) == 28U,
               "storage descriptor ABI changed");

static int syscall_storage_bind(void) {
    Process *process = scheduler_current_process();
    return process == NULL ? -13 :
        storage_service_bind(process->pid, process->generation);
}

static int syscall_storage_submit(const storage_request_submit_t *user_request,
        const uint8_t *user_data, storage_request_handle_t *user_handle) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t request_address = (uint32_t)(uintptr_t)user_request;
    uint32_t handle_address = (uint32_t)(uintptr_t)user_handle;
    if (process == NULL ||
        !user_range_accessible(directory, request_address,
                               sizeof(*user_request), false) ||
        !user_range_accessible(directory, handle_address,
                               sizeof(*user_handle), true)) return -14;
    storage_request_submit_t request;
    if (copy_from_user(&request, user_request, sizeof(request)) != 0)
        return -14;
    if (request.operation >= STORAGE_REQUEST_FORMAT_FAT12 &&
        request.operation <= STORAGE_REQUEST_FORMAT_FAT32_PREPARE &&
        process->domain_profile.kind != PROCESS_DOMAIN_ADMIN) return -13;
    uint8_t data[STORAGE_REQUEST_BLOCK_SIZE];
    const uint8_t *data_argument = NULL;
    if (request.operation == STORAGE_REQUEST_BLOCK_WRITE ||
        request.operation == STORAGE_REQUEST_VFS_WRITE) {
        uint32_t data_address = (uint32_t)(uintptr_t)user_data;
        if (request.length > sizeof(data) ||
            !user_range_accessible(directory, data_address, request.length,
                                   false) ||
            copy_from_user(data, user_data, request.length) != 0) return -14;
        data_argument = data;
    }
    if (!storage_service_component_ready()) return -112;
    storage_request_handle_t handle = STORAGE_REQUEST_INVALID_HANDLE;
    int result = storage_request_submit(process->pid, process->generation,
                                        &request, data_argument,
                                        pit_monotonic_ms(), &handle);
    if (result != 0) return result;
    if (copy_to_user_space(directory, handle_address, &handle,
                           sizeof(handle)) != 0) {
        storage_request_cancel_process(process->pid, process->generation);
        return -14;
    }
    return 0;
}

static int syscall_storage_claim(storage_request_descriptor_t *user_request,
                                 uint8_t *user_data) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t request_address = (uint32_t)(uintptr_t)user_request;
    uint32_t data_address = (uint32_t)(uintptr_t)user_data;
    if (process == NULL) return -13;
    if (!storage_service_authorized(process->pid, process->generation))
        return -13;
    if (
        !user_range_accessible(directory, request_address,
                               sizeof(*user_request), true) ||
        !user_range_accessible(directory, data_address,
                               STORAGE_REQUEST_BLOCK_SIZE, true)) return -14;
    storage_request_descriptor_t request;
    uint8_t data[STORAGE_REQUEST_BLOCK_SIZE];
    int result = storage_request_claim(process->pid, process->generation,
                                       pit_monotonic_ms(), &request, data);
    if (result != 0) return result;
    if (copy_to_user_space(directory, request_address, &request,
                           sizeof(request)) != 0 ||
        ((request.operation == STORAGE_REQUEST_BLOCK_WRITE ||
          request.operation == STORAGE_REQUEST_VFS_WRITE) &&
         copy_to_user_space(directory, data_address, data,
                            request.length) != 0)) return -14;
    return 0;
}

static int syscall_storage_block_read(uint32_t resource, uint32_t block,
                                      uint8_t *user_data) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t data_address = (uint32_t)(uintptr_t)user_data;
    if (process == NULL ||
        !storage_service_authorized(process->pid, process->generation))
        return -13;
    if (resource >= (uint32_t)drive_count ||
        block >= detected_drives[resource].sectors) return -22;
    if (!storage_service_resource_available(resource)) return -112;
    if (!user_range_accessible(directory, data_address,
                               STORAGE_REQUEST_BLOCK_SIZE, true)) return -14;
    uint8_t data[STORAGE_REQUEST_BLOCK_SIZE];
    drive_t *drive = &detected_drives[resource];
    bool read_ok = false;
#ifdef REIST_STORAGE_IO_FAULT_INJECTION
    static bool storage_io_fault_injected;
    bool inject_failure = !storage_io_fault_injected;
    if (inject_failure) {
        storage_io_fault_injected = true;
        printf("REIST_STORAGE TEST_IO_ERROR_INJECTED\n");
    }
#endif
    for (uint32_t attempt = 0U; attempt < 2U && !read_ok; ++attempt) {
#ifdef REIST_STORAGE_IO_FAULT_INJECTION
        if (inject_failure) continue;
#endif
        if (drive->type == DRIVE_TYPE_ATA ||
            drive->type == DRIVE_TYPE_AHCI ||
            drive->type == DRIVE_TYPE_PARTITION) {
            read_ok = block_device_read_sector(drive, block, data) ==
                      BLOCK_DEVICE_OK;
        } else if (drive->type == DRIVE_TYPE_FDD && drive->sector != 0U &&
                   drive->head != 0U) {
            uint32_t track_size = drive->sector * drive->head;
            uint32_t cylinder = block / track_size;
            uint32_t within = block % track_size;
            uint32_t head = within / drive->sector;
            uint32_t sector = within % drive->sector + 1U;
            if (cylinder < drive->cylinder)
                read_ok = fdc_read_sector(drive->fdd_drive_no,
                    (uint8_t)head, (uint8_t)cylinder, (uint8_t)sector, data);
        }
    }
    if (!read_ok) {
        (void)storage_service_report_io_failure(resource);
        return -5;
    }
#ifdef REIST_STORAGE_FAULT_INJECTION
    static bool storage_read_fault_injected;
    if (!storage_read_fault_injected) {
        storage_read_fault_injected = true;
        printf("REIST_STORAGE TEST_CRASH_INJECTED\n");
        task_exit_status(201);
    }
#endif
    return copy_to_user_space(directory, data_address, data, sizeof(data)) == 0
        ? 0 : -14;
}

static int syscall_storage_maintenance_acquire(uint32_t resource,
        uint32_t media_fingerprint, storage_maintenance_token_t *user_token) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t token_address = (uint32_t)(uintptr_t)user_token;
    if (process == NULL || !storage_service_authorized(process->pid,
            process->generation) || resource >= (uint32_t)drive_count ||
        !user_range_accessible(directory, token_address, sizeof(*user_token),
                                true)) return -14;
    uint32_t current_fingerprint = 0U;
    if (!storage_service_media_fingerprint(resource, &current_fingerprint) ||
        current_fingerprint != media_fingerprint ||
        !storage_service_resource_available(resource) ||
        storage_service_resource_read_only(resource)) return -30;
    storage_maintenance_token_t token = STORAGE_MAINTENANCE_INVALID_TOKEN;
    int result = storage_maintenance_acquire(process->pid,
        process->generation, resource, media_fingerprint, pit_monotonic_ms(),
        &token);
    if (result != 0) return result;
    result = vfs_maintenance_acquire(&detected_drives[resource]);
    if (result != VFS_OK) {
        (void)storage_maintenance_release(process->pid,
            process->generation, token);
        return result == VFS_ERR_BUSY ? -16 : -2;
    }
    if (copy_to_user_space(directory, token_address, &token,
                           sizeof(token)) != 0) {
        (void)vfs_maintenance_release(&detected_drives[resource]);
        (void)storage_maintenance_release(process->pid,
            process->generation, token);
        return -14;
    }
    return 0;
}

static int syscall_storage_maintenance_renew(uint32_t resource,
        uint32_t token, uint32_t media_fingerprint) {
    Process *process = scheduler_current_process();
    if (process == NULL || !storage_service_authorized(process->pid,
            process->generation) || resource >= (uint32_t)drive_count)
        return -13;
    uint32_t current_fingerprint = 0U;
    if (!storage_service_media_fingerprint(resource, &current_fingerprint) ||
        current_fingerprint != media_fingerprint) return -30;
    return storage_maintenance_renew(process->pid, process->generation, token,
                                     media_fingerprint, pit_monotonic_ms());
}

static int syscall_storage_maintenance_release(uint32_t resource,
        uint32_t token) {
    Process *process = scheduler_current_process();
    if (process == NULL || !storage_service_authorized(process->pid,
            process->generation) || resource >= (uint32_t)drive_count)
        return -13;
    int result = storage_maintenance_release(process->pid,
        process->generation, token);
    int vfs_result = vfs_maintenance_release(&detected_drives[resource]);
    if (result != 0) return result;
    return vfs_result == VFS_OK ? 0 : -2;
}

static int syscall_storage_block_write(uint32_t resource, uint32_t block,
                                       const uint8_t *user_data) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t data_address = (uint32_t)(uintptr_t)user_data;
    if (process == NULL ||
        !storage_service_authorized(process->pid, process->generation))
        return -13;
    if (resource >= (uint32_t)drive_count) return -22;
    drive_t *drive = &detected_drives[resource];
    if (block >= drive->sectors ||
        (drive->type != DRIVE_TYPE_FDD && drive->type != DRIVE_TYPE_ATA &&
         drive->type != DRIVE_TYPE_AHCI &&
         drive->type != DRIVE_TYPE_PARTITION)) return -22;
    if (!storage_service_resource_available(resource) ||
        storage_service_resource_read_only(resource)) return -30;
    if (!user_range_accessible(directory, data_address,
                               STORAGE_REQUEST_BLOCK_SIZE, false)) return -14;
    uint8_t data[STORAGE_REQUEST_BLOCK_SIZE];
    uint8_t verify[STORAGE_REQUEST_BLOCK_SIZE];
    if (copy_from_user(data, user_data, sizeof(data)) != 0) return -14;
    bool written = false;
    bool verified = false;
    if (drive->type == DRIVE_TYPE_FDD) {
        if (drive->sector == 0U || drive->head == 0U) return -22;
        uint32_t track_size = drive->sector * drive->head;
        uint32_t cylinder = block / track_size;
        uint32_t within = block % track_size;
        uint32_t head = within / drive->sector;
        uint32_t sector = within % drive->sector + 1U;
        if (cylinder >= drive->cylinder) return -22;
        written = fdc_write_sectors(drive->fdd_drive_no, (uint8_t)head,
            (uint8_t)cylinder, (uint8_t)sector, 1U, data);
        verified = written && fdc_read_sector(drive->fdd_drive_no,
            (uint8_t)head, (uint8_t)cylinder, (uint8_t)sector, verify);
    } else {
        written = block_device_write_sector(drive, block, data) ==
                  BLOCK_DEVICE_OK;
        if (written) verified = block_device_read_sector(drive, block, verify) ==
                                BLOCK_DEVICE_OK;
    }
    verified = verified && memcmp(data, verify, sizeof(data)) == 0;
    if (!verified) {
        (void)storage_service_report_media_failure(resource, true);
        return -5;
    }
    return 0;
}

static int syscall_storage_block_flush(uint32_t resource) {
    Process *process = scheduler_current_process();
    if (process == NULL || !storage_service_authorized(
            process->pid, process->generation) ||
        resource >= (uint32_t)drive_count) return -13;
    return block_device_flush(&detected_drives[resource]) == BLOCK_DEVICE_OK
        ? 0 : -5;
}

static int syscall_storage_media_commit(uint32_t resource,
                                        uint32_t *user_fingerprint) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_fingerprint;
    if (process == NULL || !storage_service_authorized(
            process->pid, process->generation)) return -13;
    if (!user_range_accessible(directory, address, sizeof(uint32_t), true))
        return -14;
    uint32_t fingerprint = 0U;
    if (!storage_service_accept_formatted_media(resource, &fingerprint))
        return -5;
    return copy_to_user_space(directory, address, &fingerprint,
                              sizeof(fingerprint)) == 0 ? 0 : -14;
}

static int syscall_storage_format_probe(uint32_t resource, uint32_t block) {
    Process *process = scheduler_current_process();
    if (process == NULL || !storage_service_authorized(
            process->pid, process->generation) ||
        resource >= (uint32_t)drive_count) return -13;
    drive_t *drive = &detected_drives[resource];
    if (drive->type != DRIVE_TYPE_PARTITION || drive->mount_point[0] != '\0' ||
        block < 2U || block >= drive->sectors ||
        !storage_service_resource_available(resource) ||
        storage_service_resource_read_only(resource)) return -30;

    uint8_t zero[STORAGE_REQUEST_BLOCK_SIZE];
    uint8_t verify[STORAGE_REQUEST_BLOCK_SIZE];
    memset(zero, 0, sizeof(zero));
    for (uint32_t attempt = 0U; attempt < 3U; ++attempt) {
        if (block_device_write_sector(drive, block, zero) == BLOCK_DEVICE_OK &&
            block_device_read_sector(drive, block, verify) == BLOCK_DEVICE_OK &&
            memcmp(zero, verify, sizeof(zero)) == 0) return 0;
    }

    uint8_t primary[STORAGE_REQUEST_BLOCK_SIZE];
    uint8_t backup[STORAGE_REQUEST_BLOCK_SIZE];
    bool controls_stable = block_device_flush(drive) == BLOCK_DEVICE_OK &&
        block_device_read_sector(drive, 0U, primary) == BLOCK_DEVICE_OK &&
        block_device_read_sector(drive, 6U, backup) == BLOCK_DEVICE_OK &&
        memcmp(primary, backup, sizeof(primary)) == 0 &&
        block_device_read_sector(drive, 0U, verify) == BLOCK_DEVICE_OK &&
        memcmp(primary, verify, sizeof(primary)) == 0;
    if (controls_stable) return 1;
    (void)storage_service_report_media_failure(resource, true);
    return -5;
}

static int syscall_storage_complete(storage_request_handle_t handle,
        int32_t result_code, const uint8_t *user_data) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    if (process == NULL ||
        !storage_service_authorized(process->pid, process->generation))
        return -13;
    uint8_t data[STORAGE_REQUEST_BLOCK_SIZE];
    const uint8_t *data_argument = NULL;
    if (result_code == 0) {
        uint32_t data_address = (uint32_t)(uintptr_t)user_data;
        if (!user_range_accessible(directory, data_address, sizeof(data),
                                   false) ||
            copy_from_user(data, user_data, sizeof(data)) != 0) return -14;
        data_argument = data;
    }
    return storage_request_complete(process->pid, process->generation,
                                    handle, result_code, data_argument);
}

static int syscall_storage_collect(storage_request_handle_t handle,
        int32_t *user_result, uint8_t *user_data) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t result_address = (uint32_t)(uintptr_t)user_result;
    if (process == NULL ||
        !user_range_accessible(directory, result_address,
                               sizeof(*user_result), true)) return -14;
    int32_t result_code = 0;
    uint32_t data_length = 0U;
    uint8_t data[STORAGE_REQUEST_BLOCK_SIZE];
    memset(data, 0, sizeof(data));
    int result = storage_request_collect_ex(process->pid, process->generation,
                                            handle, &result_code, data,
                                            &data_length);
    if (result != 0) return result;
    if (copy_to_user_space(directory, result_address, &result_code,
                           sizeof(result_code)) != 0) return -14;
    if (data_length != 0U) {
        uint32_t data_address = (uint32_t)(uintptr_t)user_data;
        if (data_length > sizeof(data) ||
            !user_range_accessible(directory, data_address, data_length, true) ||
            copy_to_user_space(directory, data_address, data,
                               data_length) != 0) return -14;
    }
    return 0;
}

static int syscall_service_connect(uint32_t service_id,
                                   ipc_handle_t *user_handle) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_handle;
    if (process == NULL ||
        !user_range_accessible(directory, address, sizeof(*user_handle), true))
        return -14;
    ipc_handle_t handle = IPC_INVALID_HANDLE;
    int result = supervisor_service_connect(process, service_id, &handle);
    if (result != 0) return result;
    return copy_to_user_space(directory, address, &handle, sizeof(handle)) == 0
        ? 0 : -14;
}

typedef struct {
    uint32_t version;
    uint32_t struct_size;
} syscall_abi_header_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t rgb;
} syscall_display_rect_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    int32_t x;
    int32_t y;
    uint32_t foreground_rgb;
    uint32_t background_rgb;
    uint32_t text_address;
    uint32_t text_length;
} syscall_display_text_t;

_Static_assert(sizeof(framebuffer_display_info_t) == 56U,
               "display information ABI size changed");
_Static_assert(sizeof(syscall_display_rect_t) == 28U,
               "display rectangle ABI size changed");
_Static_assert(sizeof(syscall_display_text_t) == 32U,
               "display text ABI size changed");

static int syscall_display_info(framebuffer_display_info_t *user_info) {
    if (!framebuffer_available()) return -19; /* ENODEV */
    syscall_abi_header_t header;
    if (copy_from_user(&header, user_info, sizeof(header)) != 0) return -14;
    if (header.version != FRAMEBUFFER_DISPLAY_ABI_VERSION ||
        header.struct_size < sizeof(framebuffer_display_info_t)) return -22;

    framebuffer_display_info_t info;
    if (!framebuffer_get_display_info(&info)) return -19;
    return copy_to_user(user_info, &info, sizeof(info)) == 0 ? 0 : -14;
}

static int syscall_display_fill_rect(const syscall_display_rect_t *user_rect) {
    if (!framebuffer_available()) return -19; /* ENODEV */
    syscall_display_rect_t rect;
    if (copy_from_user(&rect, user_rect, sizeof(rect)) != 0) return -14;
    if (rect.version != FRAMEBUFFER_DISPLAY_ABI_VERSION ||
        rect.struct_size < sizeof(rect) ||
        (rect.rgb & 0xFF000000U) != 0) return -22;

    /* Rendering is deliberately preemptible.  The framebuffer geometry is
     * immutable after boot and occasional visual tearing is preferable to a
     * Ring-3 caller monopolizing the global UP scheduler. */
    bool drawn = framebuffer_fill_rect(rect.x, rect.y, rect.width,
                                       rect.height, rect.rgb);
    return drawn ? 0 : -19;
}

static int syscall_display_draw_text(const syscall_display_text_t *user_text) {
    if (!framebuffer_available()) return -19; /* ENODEV */
    syscall_display_text_t request;
    if (copy_from_user(&request, user_text, sizeof(request)) != 0) return -14;
    if (request.version != FRAMEBUFFER_DISPLAY_ABI_VERSION ||
        request.struct_size < sizeof(request) ||
        request.text_length > FRAMEBUFFER_DISPLAY_MAX_TEXT ||
        (request.foreground_rgb & 0xFF000000U) != 0 ||
        (request.background_rgb & 0xFF000000U) != 0) return -22;
    if (request.text_length == 0) return 0;
    if (!user_range_accessible(paging_current_directory(),
                               request.text_address, request.text_length,
                               false)) return -14;

    char text[FRAMEBUFFER_DISPLAY_MAX_TEXT];
    if (copy_from_user(text, (const void*)(uintptr_t)request.text_address,
                       request.text_length) != 0) return -14;
    bool drawn = framebuffer_draw_text_pixels(
        request.x, request.y, text, request.text_length,
        request.foreground_rgb, request.background_rgb);
    return drawn ? (int)request.text_length : -19;
}

static int syscall_terminal_write(const char *user_buffer, size_t size) {
    if (size > INT_MAX ||
        !user_range_accessible(paging_current_directory(),
                               (uint32_t)(uintptr_t)user_buffer,
                               size, false)) return -14;
    char buffer[256];
    size_t total = 0;
    while (total < size) {
        size_t amount = size - total;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        if (copy_from_user(buffer, user_buffer + total, amount) != 0) return -14;
        for (size_t index = 0; index < amount; ++index)
            display_putchar(buffer[index]);
        total += amount;
    }
    return (int)total;
}

static int syscall_terminal_draw(uint32_t position, const char *user_buffer,
                                 size_t size) {
    uint32_t column = position & 0xFFFFU;
    uint32_t row = position >> 16;
    if (column >= 80U || row >= 25U || size > 80U - column) return -22;
    if (size == 0) return 0;
    if (!user_range_accessible(paging_current_directory(),
                               (uint32_t)(uintptr_t)user_buffer,
                               size, false)) return -14;
    char buffer[80];
    if (copy_from_user(buffer, user_buffer, size) != 0) return -14;
    display_write_at((int)column, (int)row, buffer, (unsigned int)size);
    return (int)size;
}

static int syscall_open(const char *user_path) {
    char path[256];
    Process *process = scheduler_current_process();
    if (process == NULL ||
        copy_string_from_user(path, sizeof(path), user_path) < 0) {
        return -14; /* EFAULT */
    }
    int descriptor = process_file_open(process, path);
    return descriptor < 0 ? -2 : descriptor; /* ENOENT/resource failure */
}

static int syscall_read(int descriptor, void *user_buffer, size_t size) {
    Process *process = scheduler_current_process();
    if (process == NULL) return -9; /* EBADF */
    if (size == 0) return 0;
    if (size > INT_MAX ||
        !user_range_accessible(paging_current_directory(),
                               (uint32_t)(uintptr_t)user_buffer, size, true)) {
        return -14; /* EFAULT */
    }

    uint8_t buffer[512];
    size_t total = 0;
    while (total < size) {
        size_t amount = size - total;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        int result = process_file_read(process, descriptor, buffer, amount);
        if (result < 0) return total != 0 ? (int)total : -9;
        if (result == 0) break;
        if (copy_to_user((uint8_t*)user_buffer + total, buffer,
                         (size_t)result) != 0) {
            return -14;
        }
        total += (size_t)result;
        if ((size_t)result < amount) break;
    }
    return (int)total;
}

static int syscall_close(int descriptor) {
    Process *process = scheduler_current_process();
    return process_file_close(process, descriptor) == 0 ? 0 : -9;
}

typedef struct {
    char name[256];
    uint32_t type;
    uint32_t size;
    uint32_t create_time;
    uint32_t modify_time;
    uint32_t access_time;
} syscall_file_info_t;

static int syscall_copy_path(char resolved[PROCESS_PATH_MAX],
                             const char *user_path) {
    char path[PROCESS_PATH_MAX];
    Process *process = scheduler_current_process();
    if (process == NULL ||
        copy_string_from_user(path, sizeof(path), user_path) < 0) return -14;
    return process_resolve_path(process, path, resolved) == 0 ? 0 : -22;
}

static int syscall_copy_file_info(void *user_info,
                                  const vfs_dir_entry_t *entry) {
    syscall_file_info_t info;
    memset(&info, 0, sizeof(info));
    strncpy(info.name, entry->name, sizeof(info.name) - 1U);
    info.type = (uint32_t)entry->type;
    info.size = entry->size;
    info.create_time = entry->create_time;
    info.modify_time = entry->modify_time;
    info.access_time = entry->access_time;
    return copy_to_user(user_info, &info, sizeof(info)) == 0 ? 0 : -14;
}

static int syscall_stat(const char *user_path, void *user_info) {
    char path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(path, user_path);
    if (result != 0) return result;
    vfs_dir_entry_t entry;
    result = vfs_stat(path, &entry);
    if (result != VFS_OK) return -2;
    return syscall_copy_file_info(user_info, &entry);
}

static int syscall_readdir(const char *user_path, uint32_t index,
                           void *user_info) {
    char path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(path, user_path);
    if (result != 0) return result;
    vfs_dir_entry_t entry;
    result = vfs_readdir(path, index, &entry);
    if (result == VFS_ERR_NOT_FOUND) return 0;
    if (result != VFS_OK) return -2;
    result = syscall_copy_file_info(user_info, &entry);
    return result == 0 ? 1 : result;
}

#define SYSCALL_READDIR_BATCH_CAPACITY 4U
static int syscall_readdir_batch(const char *user_path, uint32_t index,
                                 void *user_entries) {
    char path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(path, user_path);
    if (result != 0) return result;
    syscall_file_info_t info[SYSCALL_READDIR_BATCH_CAPACITY];
    vfs_dir_entry_t entries[SYSCALL_READDIR_BATCH_CAPACITY];
    result = vfs_readdir_batch(path, index, entries,
                               SYSCALL_READDIR_BATCH_CAPACITY);
    if (result < 0) return -5;
    if (result == 0) return 0;
    for (int i = 0; i < result; ++i) {
        memset(&info[i], 0, sizeof(info[i]));
        strncpy(info[i].name, entries[i].name, sizeof(info[i].name) - 1U);
        info[i].type = (uint32_t)entries[i].type;
        info[i].size = entries[i].size;
        info[i].create_time = entries[i].create_time;
        info[i].modify_time = entries[i].modify_time;
        info[i].access_time = entries[i].access_time;
    }
    size_t bytes = (size_t)result * sizeof(info[0]);
    return copy_to_user(user_entries, info, bytes) == 0 ? result : -14;
}

static int syscall_create(const char *user_path) {
    char path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(path, user_path);
    if (result != 0) return result;
    Process *process = scheduler_current_process();
    int descriptor = process_file_create(process, path);
    return descriptor < 0 ? -5 : descriptor;
}

static int syscall_touch(const char *user_path) {
    char path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(path, user_path);
    if (result != 0) return result;
    return vfs_touch(path) == VFS_OK ? 0 : -5;
}

static int syscall_write(int descriptor, const void *user_buffer, size_t size) {
    Process *process = scheduler_current_process();
    if (process == NULL) return -9;
    if (size == 0) return 0;
    if (size > INT_MAX ||
        !user_range_accessible(paging_current_directory(),
                               (uint32_t)(uintptr_t)user_buffer, size, false)) {
        return -14;
    }
    uint8_t buffer[512];
    size_t total = 0;
    while (total < size) {
        size_t amount = size - total;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        if (copy_from_user(buffer, (const uint8_t*)user_buffer + total,
                           amount) != 0) return -14;
        int written = process_file_write(process, descriptor, buffer, amount);
        if (written < 0) return total != 0 ? (int)total : -9;
        if (written == 0) break;
        total += (size_t)written;
        if ((size_t)written < amount) break;
    }
    return (int)total;
}

static int syscall_fsync(int descriptor) {
    return process_file_sync(scheduler_current_process(), descriptor) == 0
        ? 0 : -5;
}

static int syscall_unlink(const char *user_path) {
    char path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(path, user_path);
    if (result != 0) return result;
    return process_file_unlink(scheduler_current_process(), path) == 0 ? 0 : -2;
}

static int syscall_rename(const char *user_old_path,
                          const char *user_new_path) {
    char old_path[PROCESS_PATH_MAX];
    char new_path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(old_path, user_old_path);
    if (result != 0) return result;
    result = syscall_copy_path(new_path, user_new_path);
    if (result != 0) return result;
    return vfs_rename(old_path, new_path) == VFS_OK ? 0 : -5;
}

static int syscall_getpid(void) {
    Process *process = scheduler_current_process();
    return process != NULL ? process->pid : -3;
}

static int syscall_spawn(const char *user_path) {
    char path[PROCESS_PATH_MAX];
    Process *parent = scheduler_current_process();
    if (parent == NULL ||
        copy_string_from_user(path, sizeof(path), user_path) < 0) return -14;
    int pid = process_spawn(parent, path);
    return pid < 0 ? -2 : pid;
}

#define SYSCALL_MAX_ARGUMENTS 16
#define SYSCALL_ARGUMENT_CAPACITY 256
static int syscall_spawnv(const char *user_path, const char *const *user_argv,
                          int argc) {
    if (argc < 1 || argc > SYSCALL_MAX_ARGUMENTS || user_argv == NULL) {
        return -22;
    }
    char path[PROCESS_PATH_MAX];
    char *arguments = (char*)k_malloc(
        (size_t)argc * SYSCALL_ARGUMENT_CAPACITY);
    const char *argument_list[SYSCALL_MAX_ARGUMENTS];
    if (arguments == NULL) return -12;

    Process *parent = scheduler_current_process();
    if (parent == NULL ||
        copy_string_from_user(path, sizeof(path), user_path) < 0) {
        k_free(arguments);
        return -14;
    }

    for (int index = 0; index < argc; ++index) {
        const char *user_argument;
        char *argument = arguments +
                         (size_t)index * SYSCALL_ARGUMENT_CAPACITY;
        if (copy_from_user(&user_argument, user_argv + index,
                           sizeof(user_argument)) != 0 ||
            copy_string_from_user(argument, SYSCALL_ARGUMENT_CAPACITY,
                                  user_argument) < 0) {
            k_free(arguments);
            return -14;
        }
        argument_list[index] = argument;
    }
    int result = process_spawn_args(parent, path, argc, argument_list);
    k_free(arguments);
    return result;
}

static int syscall_wait(int pid, int *user_status) {
    Process *parent = scheduler_current_process();
    if (parent == NULL ||
        !user_range_accessible(paging_current_directory(),
                               (uint32_t)(uintptr_t)user_status,
                               sizeof(*user_status), true)) return -14;
    for (;;) {
        int status = 0;
        /* On this uniprocessor kernel, keeping interrupts disabled makes the
         * child-state check and TASK_WAITING registration one atomic
         * operation.  Otherwise a child can exit between both operations and
         * its wakeup is lost permanently. */
        uint32_t flags = irq_save();
        wait_queue_t *wait_queue = NULL;
        int result = process_wait_status_locked(parent, pid, &status,
                                                &wait_queue);
        if (result < 0) {
            irq_restore(flags);
            return -10;
        }
        if (result > 0) {
            irq_restore(flags);
            (void)scheduler_reap_finished_tasks();
            return copy_to_user(user_status, &status, sizeof(status)) == 0
                       ? pid : -14;
        }
        if (wait_queue == NULL ||
            wait_queue_block_locked(wait_queue, TASK_BLOCK_WAITING) != 0) {
            irq_restore(flags);
            return -11;
        }
        irq_restore(flags);
    }
}

static int syscall_process_info(uint32_t index, void *user_info) {
    process_info_t info;
    int result = process_get_info(index, &info);
    if (result <= 0) return result;
    return copy_to_user(user_info, &info, sizeof(info)) == 0 ? 1 : -14;
}

static int syscall_kill(int pid) {
    Process *caller = scheduler_current_process();
    if (caller == NULL || pid <= 0 || pid == caller->pid) return -22;
    return process_terminate_authorized(caller, pid) == 0 ? 0 : -13;
}

static int syscall_getcwd(void *user_buffer, size_t size) {
    if (size == 0 || size > PROCESS_PATH_MAX) return -22;
    char path[PROCESS_PATH_MAX];
    if (process_get_working_directory(scheduler_current_process(), path,
                                      size) != 0) return -34;
    size_t length = strlen(path) + 1U;
    return copy_to_user(user_buffer, path, length) == 0 ? 0 : -14;
}

static int syscall_chdir(const char *user_path) {
    char path[PROCESS_PATH_MAX];
    Process *process = scheduler_current_process();
    if (process == NULL ||
        copy_string_from_user(path, sizeof(path), user_path) < 0) return -14;
    return process_set_working_directory(process, path) == 0 ? 0 : -2;
}

typedef struct {
    uint32_t type;
    char name[8];
    char mount_point[64];
    uint32_t sectors;
} syscall_drive_info_t;

static int syscall_drive_info(uint32_t index, void *user_info) {
    if (index >= (uint32_t)drive_count) return 0;
    drive_t *drive = &detected_drives[index];
    syscall_drive_info_t info;
    memset(&info, 0, sizeof(info));
    info.type = (uint32_t)drive->type;
    info.sectors = drive->sectors;
    strncpy(info.name, drive->name, sizeof(info.name) - 1U);
    strncpy(info.mount_point, drive->mount_point,
            sizeof(info.mount_point) - 1U);
    return copy_to_user(user_info, &info, sizeof(info)) == 0 ? 1 : -14;
}

typedef struct {
    uint32_t version, struct_size, resource, first_lba;
    uint32_t sectors, type, confirm;
} syscall_partition_request_t;

static int syscall_partition_create(const void *user_request) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t address = (uint32_t)(uintptr_t)user_request;
    if (process == NULL || process->domain_profile.kind != PROCESS_DOMAIN_ADMIN ||
        !user_range_accessible(directory, address,
                               sizeof(syscall_partition_request_t), false)) return -13;
    syscall_partition_request_t request;
    if (copy_from_user(&request, user_request, sizeof(request)) != 0 ||
        request.version != 1U || request.struct_size != sizeof(request) ||
        request.confirm != 0x52454953U || request.resource >= (uint32_t)drive_count)
        return -22;
    drive_t *drive = &detected_drives[request.resource];
    if ((drive->type != DRIVE_TYPE_ATA && drive->type != DRIVE_TYPE_AHCI) ||
        drive->has_partitions || drive->mount_point[0] == '/') return -1001;
    for (uint32_t index = 0U; index < (uint32_t)drive_count; ++index)
        if (detected_drives[index].type == DRIVE_TYPE_PARTITION &&
            detected_drives[index].parent_resource == request.resource)
            return -16;
    int result = partition_provision_mbr(drive, request.first_lba,
                                         request.sectors, (uint8_t)request.type);
    if (result == 0 && (partition_discover() == 0U ||
        !storage_service_accept_partition_layout(request.resource))) {
        (void)storage_service_report_media_failure(request.resource, true);
        return -5;
    }
    return result;
}

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t flags;
    uint32_t reserved;
} syscall_drive_status_t;

#define DRIVE_STATUS_VERSION 1U
#define DRIVE_STATUS_AVAILABLE   (1U << 0)
#define DRIVE_STATUS_READ_ONLY   (1U << 1)
#define DRIVE_STATUS_DEGRADED    (1U << 2)
#define DRIVE_STATUS_QUARANTINED (1U << 3)
#define DRIVE_STATUS_RECOVERING  (1U << 4)

static int syscall_drive_status(uint32_t index, void *user_status) {
    syscall_drive_status_t request;
    if (index >= (uint32_t)drive_count || user_status == NULL ||
        copy_from_user(&request, user_status, sizeof(request)) != 0 ||
        request.version != DRIVE_STATUS_VERSION ||
        request.struct_size != sizeof(request)) return -22;

    syscall_drive_status_t status = {
        .version = DRIVE_STATUS_VERSION,
        .struct_size = sizeof(status),
        .flags = 0U,
        .reserved = 0U
    };
    bool available = storage_service_resource_available(index);
    bool read_only = storage_service_resource_read_only(index);
    bool recovering = storage_service_resource_recovering(index);
    if (available) status.flags |= DRIVE_STATUS_AVAILABLE;
    if (read_only) status.flags |= DRIVE_STATUS_READ_ONLY;
    if (!available || read_only) status.flags |= DRIVE_STATUS_DEGRADED;
    if (!available) status.flags |= DRIVE_STATUS_QUARANTINED;
    if (recovering) status.flags |= DRIVE_STATUS_RECOVERING;
    return copy_to_user(user_status, &status, sizeof(status)) == 0 ? 0 : -14;
}

static int syscall_admin_storage(
        const admin_storage_request_t *user_request,
        admin_storage_result_t *user_result) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t request_address = (uint32_t)(uintptr_t)user_request;
    uint32_t result_address = (uint32_t)(uintptr_t)user_result;
    if (process == NULL ||
        process->domain_profile.kind != PROCESS_DOMAIN_ADMIN ||
        !user_range_accessible(directory, request_address,
                               sizeof(admin_storage_request_t), false) ||
        !user_range_accessible(directory, result_address,
                               sizeof(admin_storage_result_t), true))
        return -13;
    admin_storage_request_t request;
    admin_storage_result_t result;
    if (copy_from_user(&request, user_request, sizeof(request)) != 0)
        return -14;
    int status = admin_maintenance_execute(
        process->pid, process->generation, &request, &result,
        pit_monotonic_ms());
    if (status != 0) return status;
    return copy_to_user_space(directory, result_address, &result,
                              sizeof(result)) == 0 ? 0 : -14;
}

_Static_assert(sizeof(component_control_request_t) == 24U,
               "component control request ABI changed");
_Static_assert(sizeof(component_control_result_t) == 56U,
               "component control result ABI changed");

static int syscall_component_control(
        const component_control_request_t *user_request,
        component_control_result_t *user_result) {
    Process *process = scheduler_current_process();
    page_directory_t *directory = paging_current_directory();
    uint32_t request_address = (uint32_t)(uintptr_t)user_request;
    uint32_t result_address = (uint32_t)(uintptr_t)user_result;
    if (process == NULL ||
        process->domain_profile.kind != PROCESS_DOMAIN_COMPONENT_ADMIN)
        return -13;
    if (!user_range_accessible(directory, request_address,
                               sizeof(component_control_request_t), false) ||
        !user_range_accessible(directory, result_address,
                               sizeof(component_control_result_t), true))
        return -14;
    component_control_request_t request;
    component_control_result_t result;
    if (copy_from_user(&request, user_request, sizeof(request)) != 0)
        return -14;
    int status = component_control_execute(
        process->pid, process->generation, &request, &result,
        pit_monotonic_ms());
    if (status != 0) return status;
    return copy_to_user_space(directory, result_address, &result,
                              sizeof(result)) == 0 ? 0 : -14;
}

static int syscall_space(const char *user_path, void *user_info) {
    char path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(path, user_path);
    if (result != 0) return result;
    vfs_space_info_t info;
    result = vfs_space(path, &info);
    if (result != VFS_OK) return -5;
    return copy_to_user(user_info, &info, sizeof(info)) == 0 ? 0 : -14;
}

static int syscall_mkdir(const char *user_path) {
    char path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(path, user_path);
    if (result != 0) return result;
    return vfs_mkdir(path) == VFS_OK ? 0 : -5;
}

static int syscall_rmdir(const char *user_path) {
    char path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(path, user_path);
    if (result != 0) return result;
    return vfs_rmdir(path) == VFS_OK ? 0 : -5;
}

//---------------------------------------------------------------------------------------------
// System Call Table
//---------------------------------------------------------------------------------------------

/**
 * Global syscall dispatch table - maps syscall numbers to function pointers
 * User programs trigger INT 0x80 with syscall number in EAX
 */
void* syscall_table[512] __attribute__((section(".syscall_table"))) = {
    (void*)&display_putchar,            // Syscall 0: Write character to display
    (void*)&kernel_print_number,        // Syscall 1: Print number (for testing)
    (void*)&scheduler_sleep_ms,         // Syscall 2: Compatible blocking delay
    (void*)&kb_wait_enter,              // Syscall 3: Wait for Enter key
    (void*)&process_user_malloc,        // Syscall 4: Process-local allocation
    (void*)&process_user_free,          // Syscall 5: Release user allocation
    (void*)&process_user_realloc,       // Syscall 6: Resize user allocation
    (void*)&getchar,                    // Syscall 7: Read character from keyboard
    NULL,                               // Syscall 8: reserved (IRQ registration is privileged)
    (void*)&task_exit,                  // Syscall 9: Terminate current task
    (void*)&syscall_get_date,           // Syscall 10: Packed RTC date
    (void*)&syscall_get_time,           // Syscall 11: Packed RTC time
    (void*)&pit_ticks,                  // Syscall 12: Milliseconds since boot
    (void*)&syscall_memory_kb,          // Syscall 13: Usable memory in KiB
    (void*)&syscall_open,               // Syscall 14: Open read-only file
    (void*)&syscall_read,               // Syscall 15: Read from descriptor
    (void*)&syscall_close,              // Syscall 16: Close descriptor
    (void*)&syscall_stat,               // Syscall 17: Get path metadata
    (void*)&syscall_readdir,            // Syscall 18: Read directory entry
    (void*)&syscall_create,             // Syscall 19: Create writable file
    (void*)&syscall_write,              // Syscall 20: Write descriptor
    (void*)&syscall_unlink,             // Syscall 21: Delete file
    (void*)&syscall_getpid,             // Syscall 22: Current process ID
    (void*)&syscall_spawn,              // Syscall 23: Start child process
    (void*)&syscall_wait,               // Syscall 24: Collect child status
    (void*)&syscall_readdir_batch,      // Syscall 25: Read directory batch
    (void*)&syscall_process_info,       // Syscall 26: Enumerate processes
    (void*)&syscall_kill,               // Syscall 27: Terminate a process
    (void*)&syscall_getcwd,             // Syscall 28: Current directory
    (void*)&syscall_chdir,              // Syscall 29: Change directory
    (void*)&syscall_spawnv,             // Syscall 30: Spawn with arguments
    (void*)&syscall_drive_info,         // Syscall 31: Mounted drive metadata
    (void*)&syscall_space,              // Syscall 32: Filesystem capacity
    (void*)&syscall_mkdir,              // Syscall 33: Create directory
    (void*)&syscall_rmdir,              // Syscall 34: Remove directory
    (void*)&display_clear,              // Syscall 35: Clear terminal
    (void*)&display_set_cursor,         // Syscall 36: Set terminal cursor
    (void*)&syscall_terminal_write,     // Syscall 37: Write terminal buffer
    (void*)&syscall_terminal_draw,      // Syscall 38: Draw text at position
    (void*)&getchar_nonblocking,        // Syscall 39: Poll terminal input
    (void*)&scheduler_yield,            // Syscall 40: Yield current time slice
    (void*)&scheduler_sleep_ms,         // Syscall 41: Blocking sleep in ms
    (void*)&syscall_monotonic_ms,       // Syscall 42: 64-bit monotonic time
    (void*)&syscall_memory_stats,       // Syscall 43: Physical/heap metrics
    (void*)&syscall_display_info,       // Syscall 44: Versioned display info
    (void*)&syscall_display_fill_rect,  // Syscall 45: Clipped RGB rectangle
    (void*)&syscall_display_draw_text,  // Syscall 46: Clipped pixel text
    (void*)&syscall_rename,             // Syscall 47: Atomic same-FS rename
    (void*)&syscall_fsync,              // Syscall 48: Persist writable file
    (void*)&syscall_ipc_create,         // Syscall 49: Create IPC endpoint
    (void*)&syscall_ipc_send,           // Syscall 50: Send bounded message
    (void*)&syscall_ipc_receive,        // Syscall 51: Receive/block on endpoint
    (void*)&syscall_ipc_close,          // Syscall 52: Revoke owned endpoint
    (void*)&syscall_ipc_send_timeout,   // Syscall 53: Timed IPC send
    (void*)&syscall_ipc_receive_timeout,// Syscall 54: Timed IPC receive
    (void*)&syscall_ipc_delegate,       // Syscall 55: Attenuated delegation
    (void*)&syscall_reist_report,       // Syscall 56: Probe health report
    (void*)&syscall_service_connect,    // Syscall 57: Connect named service
    (void*)&ipc_release,                // Syscall 58: Release delegated cap
    (void*)&syscall_network_probe,      // Syscall 59: Fixed supervised probe
    (void*)&syscall_network_probe_id,   // Syscall 60: Probe with monotone ID
    (void*)&syscall_network_probe_stats,// Syscall 61: Read degradation stats
    (void*)&syscall_reist_arp_binding,  // Syscall 62: Commit bounded ARP binding
    (void*)&syscall_reist_arp_reply,    // Syscall 63: Mediated ARP reply
    (void*)&syscall_reist_arp_resolution,// Syscall 64: Mediated ARP request
    (void*)&syscall_network_arp_resolve, // Syscall 65: Request ARP resolution
    (void*)&syscall_storage_bind,       // Syscall 66: Bind storage service
    (void*)&syscall_storage_submit,     // Syscall 67: Submit storage request
    (void*)&syscall_storage_claim,      // Syscall 68: Claim storage request
    (void*)&syscall_storage_block_read, // Syscall 69: Mediated block read
    (void*)&syscall_storage_complete,   // Syscall 70: Complete storage request
    (void*)&syscall_storage_collect,    // Syscall 71: Collect storage request
    (void*)&syscall_reist_icmp_echo_reply,// Syscall 72: Mediated ICMP echo
    (void*)&syscall_reist_dhcp_commit,   // Syscall 73: Mediated DHCP config
    (void*)&syscall_reist_udp_echo_reply,// Syscall 74: Mediated UDP echo
    (void*)&syscall_reist_udp_bind,      // Syscall 75: Bind supervised UDP
    (void*)&syscall_reist_udp_unbind,    // Syscall 76: Unbind supervised UDP
    (void*)&syscall_reist_udp_reply,     // Syscall 77: Reply on UDP binding
    (void*)&syscall_reist_dhcp_renew,    // Syscall 78: Bounded DHCP renew/rebind
    (void*)&syscall_reist_network_frame, // Syscall 79: Bounded raw RX handoff
    (void*)&syscall_reist_udp_ingress,   // Syscall 80: Validate Ring-3 UDP ingress
    (void*)&syscall_reist_dhcp_ingress,  // Syscall 81: Validate Ring-3 DHCP ingress
    (void*)&syscall_reist_dhcp_boot_start,// Syscall 82: Start bounded boot DHCP
    (void*)&syscall_reist_icmp_ingress,   // Syscall 83: Validate Ring-3 ICMP ingress
    (void*)&syscall_scheduler_stats,       // Syscall 84: Bounded task-slot metrics
    (void*)&syscall_storage_block_write,   // Syscall 85: Verified FDD block write
    (void*)&syscall_storage_maintenance_acquire, // Syscall 86
    (void*)&syscall_storage_maintenance_renew,   // Syscall 87
    (void*)&syscall_storage_maintenance_release, // Syscall 88
    (void*)&syscall_drive_status,        // Syscall 89: Versioned drive health
    (void*)&syscall_admin_storage,       // Syscall 90: Bounded storage admin
    (void*)&syscall_component_control,   // Syscall 91: Static components
    (void*)&syscall_partition_create,    // Syscall 92: Verified MBR create
    (void*)&syscall_storage_block_flush, // Syscall 93: Explicit write barrier
    (void*)&syscall_storage_media_commit,// Syscall 94: Accept formatted identity
    (void*)&syscall_storage_format_probe,// Syscall 95: Isolated sector probe
    (void*)&syscall_network_control,     // Syscall 96: Userspace LAN control
    (void*)&syscall_udp_socket_control,  // Syscall 97: Bounded UDP sockets
    (void*)&syscall_udp_socket_sendto,   // Syscall 98: UDP datagram transmit
    (void*)&syscall_udp_socket_recvfrom, // Syscall 99: Nonblocking UDP receive
    (void*)&syscall_udp_socket_ingress,  // Syscall 100: Ring-3 validated ingress
    (void*)&syscall_tcp_socket_control,  // Syscall 101: Bounded TCP sockets
    (void*)&syscall_tcp_socket_connect,  // Syscall 102: Active TCP open
    (void*)&syscall_tcp_socket_send,     // Syscall 103: Acknowledged TCP send
    (void*)&syscall_tcp_socket_receive,  // Syscall 104: Timed stream receive
    (void*)&syscall_tcp_socket_ingress,  // Syscall 105: Validated TCP ingress
    (void*)&syscall_tcp_socket_listen,   // Syscall 106: Passive TCP open
    (void*)&syscall_tcp_socket_accept,   // Syscall 107: Bounded accept
    (void*)&syscall_touch,                // Syscall 108: Update file timestamps
    // Add more syscalls here as needed
};

//---------------------------------------------------------------------------------------------
// System Call Handler
//---------------------------------------------------------------------------------------------

/**
 * Main syscall dispatcher - called from INT 0x80 handler in arch/x86/cpu/syscall.asm
 * 
 * Retrieves syscall number and arguments from CPU registers:
 * - EAX: syscall number
 * - EBX: argument 1
 * - ECX: argument 2
 * - EDX: argument 3
 * 
 * @param regs Saved register frame built by syscall_handler_asm
 */
void syscall_handler(Registers* regs) {
    const uint32_t syscall_index = regs->eax;
    const uint32_t arg1 = regs->ebx;
    const uint32_t arg2 = regs->ecx;
    const uint32_t arg3 = regs->edx;
    uint32_t result = 0;

    bool user_call = (regs->cs & 3U) == 3U;
    Process *authority_process = user_call ? scheduler_current_process() : NULL;
    if (user_call && (authority_process == NULL ||
        !process_syscall_allowed(authority_process, syscall_index))) {
        regs->eax = (uint32_t)-13;
        return;
    }

    // Validate syscall index
    if (syscall_index >= 512 || syscall_table[syscall_index] == 0) {
        printf("Invalid syscall index: %u\n", syscall_index);
        regs->eax = (uint32_t)-1;
        return;
    }

    switch (syscall_index) {
        case SYS_TERMINAL_PUTCHAR:
            display_putchar((char)arg1);
            break;
        case SYS_PRINT:
            kernel_print_number((int)arg1);
            break;
        case SYS_DELAY:
            result = (uint32_t)syscall_delay(regs, arg1);
            break;
        case SYS_WAIT_ENTER:
            kb_wait_enter();
            break;
        case SYS_MALLOC:
            result = (uint32_t)(uintptr_t)process_user_malloc((size_t)arg1);
            if (result == 0) result = (uint32_t)-12; /* ENOMEM */
            break;
        case SYS_FREE:
            result = process_user_free((void*)(uintptr_t)arg1) == 0
                ? 0U : (uint32_t)-22; /* EINVAL */
            break;
        case SYS_REALLOC:
            result = (uint32_t)(uintptr_t)process_user_realloc(
                (void*)(uintptr_t)arg1, (size_t)arg2);
            if (result == 0 && arg2 != 0) result = (uint32_t)-12;
            break;
        case SYS_TERMINAL_GETCHAR:
            result = (uint32_t)(uint8_t)getchar();
            if (result == 0x03U) {
                printf("^C\n");
                task_exit_status(130);
            }
            break;
        case SYS_EXIT:
            task_exit_status((int)arg1);
        case SYS_GET_DATE:
            result = syscall_get_date();
            break;
        case SYS_GET_TIME:
            result = syscall_get_time();
            break;
        case SYS_UPTIME_MS:
            result = pit_ticks();
            break;
        case SYS_MEMORY_KB:
            result = syscall_memory_kb();
            break;
        case SYS_OPEN:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_open((const char*)(uintptr_t)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_READ:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_read((int)arg1,
                                            (void*)(uintptr_t)arg2,
                                            (size_t)arg3);
            scheduler_preempt_enable();
            break;
        case SYS_CLOSE:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_close((int)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_STAT:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_stat(
                (const char*)(uintptr_t)arg1, (void*)(uintptr_t)arg2);
            scheduler_preempt_enable();
            break;
        case SYS_READDIR:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_readdir(
                (const char*)(uintptr_t)arg1, arg2, (void*)(uintptr_t)arg3);
            scheduler_preempt_enable();
            break;
        case SYS_CREATE:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_create((const char*)(uintptr_t)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_TOUCH:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_touch((const char*)(uintptr_t)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_WRITE:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_write(
                (int)arg1, (const void*)(uintptr_t)arg2, (size_t)arg3);
            scheduler_preempt_enable();
            break;
        case SYS_UNLINK:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_unlink((const char*)(uintptr_t)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_GETPID:
            result = (uint32_t)syscall_getpid();
            break;
        case SYS_SPAWN:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_spawn((const char*)(uintptr_t)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_WAIT:
            result = (uint32_t)syscall_wait(
                (int)arg1, (int*)(uintptr_t)arg2);
            break;
        case SYS_READDIR_BATCH:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_readdir_batch(
                (const char*)(uintptr_t)arg1, arg2,
                (void*)(uintptr_t)arg3);
            scheduler_preempt_enable();
            break;
        case SYS_PROCESS_INFO:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_process_info(
                arg1, (void*)(uintptr_t)arg2);
            scheduler_preempt_enable();
            break;
        case SYS_KILL:
            result = (uint32_t)syscall_kill((int)arg1);
            break;
        case SYS_GETCWD:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_getcwd(
                (void*)(uintptr_t)arg1, (size_t)arg2);
            scheduler_preempt_enable();
            break;
        case SYS_CHDIR:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_chdir(
                (const char*)(uintptr_t)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_SPAWNV:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_spawnv(
                (const char*)(uintptr_t)arg1,
                (const char* const*)(uintptr_t)arg2, (int)arg3);
            scheduler_preempt_enable();
            break;
        case SYS_DRIVE_INFO:
            result = (uint32_t)syscall_drive_info(
                arg1, (void*)(uintptr_t)arg2);
            break;
        case SYS_SPACE:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_space(
                (const char*)(uintptr_t)arg1, (void*)(uintptr_t)arg2);
            scheduler_preempt_enable();
            break;
        case SYS_MKDIR:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_mkdir((const char*)(uintptr_t)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_RMDIR:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_rmdir((const char*)(uintptr_t)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_CLEAR:
            display_clear();
            break;
        case SYS_SET_CURSOR:
            if (arg1 < 80U && arg2 < 25U)
                display_set_cursor((int)arg1, (int)arg2);
            else
                result = (uint32_t)-22;
            break;
        case SYS_TERMINAL_WRITE:
            result = (uint32_t)syscall_terminal_write(
                (const char*)(uintptr_t)arg1, (size_t)arg2);
            break;
        case SYS_TERMINAL_DRAW:
            result = (uint32_t)syscall_terminal_draw(
                arg1, (const char*)(uintptr_t)arg2, (size_t)arg3);
            break;
        case SYS_GETCHAR_NONBLOCKING:
            result = (uint32_t)(uint8_t)getchar_nonblocking();
            break;
        case SYS_YIELD:
            result = (uint32_t)scheduler_yield();
            break;
        case SYS_SLEEP_MS:
            result = (uint32_t)syscall_delay(regs, arg1);
            break;
        case SYS_MONOTONIC_MS:
            result = (uint32_t)syscall_monotonic_ms(
                (uint64_t*)(uintptr_t)arg1);
            break;
        case SYS_MEMORY_STATS:
            result = (uint32_t)syscall_memory_stats(
                (memory_stats_t*)(uintptr_t)arg1, arg2, arg3);
            break;
        case SYS_DISPLAY_INFO:
            result = (uint32_t)syscall_display_info(
                (framebuffer_display_info_t*)(uintptr_t)arg1);
            break;
        case SYS_FILL_RECT:
            result = (uint32_t)syscall_display_fill_rect(
                (const syscall_display_rect_t*)(uintptr_t)arg1);
            break;
        case SYS_DRAW_TEXT:
            result = (uint32_t)syscall_display_draw_text(
                (const syscall_display_text_t*)(uintptr_t)arg1);
            break;
        case SYS_RENAME:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_rename(
                (const char*)(uintptr_t)arg1,
                (const char*)(uintptr_t)arg2);
            scheduler_preempt_enable();
            break;
        case SYS_FSYNC:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_fsync((int)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_IPC_CREATE:
            result = (uint32_t)syscall_ipc_create(
                (ipc_handle_t*)(uintptr_t)arg1);
            break;
        case SYS_IPC_SEND:
            result = (uint32_t)syscall_ipc_send(
                (ipc_handle_t)arg1,
                (const ipc_message_t*)(uintptr_t)arg2);
            break;
        case SYS_IPC_RECEIVE:
            result = (uint32_t)syscall_ipc_receive(
                (ipc_handle_t)arg1, (ipc_message_t*)(uintptr_t)arg2);
            break;
        case SYS_IPC_CLOSE:
            result = (uint32_t)syscall_ipc_close((ipc_handle_t)arg1);
            break;
        case SYS_IPC_SEND_TIMEOUT:
            result = (uint32_t)syscall_ipc_send_timeout(
                (ipc_handle_t)arg1,
                (const ipc_message_t*)(uintptr_t)arg2, arg3);
            break;
        case SYS_IPC_RECEIVE_TIMEOUT:
            result = (uint32_t)syscall_ipc_receive_timeout(
                (ipc_handle_t)arg1,
                (ipc_message_t*)(uintptr_t)arg2, arg3);
            break;
        case SYS_IPC_DELEGATE:
            result = (uint32_t)syscall_ipc_delegate(
                (ipc_handle_t)arg1, (int)arg2, arg3);
            break;
        case SYS_REIST_REPORT:
            result = (uint32_t)syscall_reist_report(arg1, arg2);
            break;
        case SYS_SERVICE_CONNECT:
            result = (uint32_t)syscall_service_connect(
                arg1, (ipc_handle_t*)(uintptr_t)arg2);
            break;
        case SYS_IPC_RELEASE:
            result = (uint32_t)ipc_release(
                scheduler_current_process(), (ipc_handle_t)arg1);
            break;
        case SYS_NETWORK_PROBE:
            result = (uint32_t)syscall_network_probe();
            break;
        case SYS_NETWORK_PROBE_ID:
            result = (uint32_t)syscall_network_probe_id(
                (uint32_t*)(uintptr_t)arg1);
            break;
        case SYS_NETWORK_PROBE_STATS:
            result = (uint32_t)syscall_network_probe_stats(
                (syscall_network_probe_stats_t*)(uintptr_t)arg1, arg2, arg3);
            break;
        case SYS_REIST_ARP_BINDING:
            result = (uint32_t)syscall_reist_arp_binding(
                (const supervisor_arp_binding_t*)(uintptr_t)arg1);
            break;
        case SYS_REIST_ARP_REPLY:
            result = (uint32_t)syscall_reist_arp_reply(
                (const supervisor_arp_reply_t*)(uintptr_t)arg1);
            break;
        case SYS_REIST_ARP_RESOLUTION:
            result = (uint32_t)syscall_reist_arp_resolution(
                (const supervisor_arp_resolution_t*)(uintptr_t)arg1);
            break;
        case SYS_NETWORK_ARP_RESOLVE:
            result = (uint32_t)syscall_network_arp_resolve(arg1);
            break;
        case SYS_STORAGE_BIND:
            result = (uint32_t)syscall_storage_bind();
            break;
        case SYS_STORAGE_SUBMIT:
            result = (uint32_t)syscall_storage_submit(
                (const storage_request_submit_t*)(uintptr_t)arg1,
                (const uint8_t*)(uintptr_t)arg2,
                (storage_request_handle_t*)(uintptr_t)arg3);
            break;
        case SYS_STORAGE_CLAIM:
            result = (uint32_t)syscall_storage_claim(
                (storage_request_descriptor_t*)(uintptr_t)arg1,
                (uint8_t*)(uintptr_t)arg2);
            break;
        case SYS_STORAGE_BLOCK_READ:
            result = (uint32_t)syscall_storage_block_read(
                arg1, arg2, (uint8_t*)(uintptr_t)arg3);
            break;
        case SYS_STORAGE_COMPLETE:
            result = (uint32_t)syscall_storage_complete(
                arg1, (int32_t)arg2, (const uint8_t*)(uintptr_t)arg3);
            break;
        case SYS_STORAGE_COLLECT:
            result = (uint32_t)syscall_storage_collect(
                arg1, (int32_t*)(uintptr_t)arg2,
                (uint8_t*)(uintptr_t)arg3);
            break;
        case SYS_REIST_ICMP_ECHO_REPLY:
            result = (uint32_t)syscall_reist_icmp_echo_reply(
                (const supervisor_icmp_echo_reply_t*)(uintptr_t)arg1);
            break;
        case SYS_REIST_DHCP_COMMIT:
            result = (uint32_t)syscall_reist_dhcp_commit(
                (const supervisor_dhcp_commit_t*)(uintptr_t)arg1);
            break;
        case SYS_REIST_UDP_ECHO_REPLY:
            result = (uint32_t)syscall_reist_udp_echo_reply(
                (const supervisor_udp_echo_reply_t*)(uintptr_t)arg1);
            break;
        case SYS_REIST_UDP_BIND:
            result = (uint32_t)syscall_reist_udp_bind(
                (const supervisor_udp_bind_request_t*)(uintptr_t)arg1,
                (supervisor_udp_binding_handle_t*)(uintptr_t)arg2);
            break;
        case SYS_REIST_UDP_UNBIND:
            result = (uint32_t)syscall_reist_udp_unbind(arg1);
            break;
        case SYS_REIST_UDP_REPLY:
            result = (uint32_t)syscall_reist_udp_reply(
                (const supervisor_udp_reply_t*)(uintptr_t)arg1);
            break;
        case SYS_REIST_DHCP_RENEW:
            result = (uint32_t)syscall_reist_dhcp_renew(
                (const supervisor_dhcp_renew_request_t*)(uintptr_t)arg1);
            break;
        case SYS_REIST_NETWORK_FRAME:
            result = (uint32_t)syscall_reist_network_frame(
                (supervisor_network_frame_t*)(uintptr_t)arg1);
            break;
        case SYS_REIST_UDP_INGRESS:
            result = (uint32_t)syscall_reist_udp_ingress(
                (supervisor_udp_ingress_t*)(uintptr_t)arg1,
                (const uint8_t*)(uintptr_t)arg2);
            break;
        case SYS_REIST_DHCP_INGRESS:
            result = (uint32_t)syscall_reist_dhcp_ingress(
                (const supervisor_dhcp_ingress_t*)(uintptr_t)arg1);
            break;
        case SYS_REIST_DHCP_BOOT_START:
            result = (uint32_t)syscall_reist_dhcp_boot_start(
                (const supervisor_dhcp_boot_start_t*)(uintptr_t)arg1);
            break;
        case SYS_REIST_ICMP_INGRESS:
            result = (uint32_t)syscall_reist_icmp_ingress(
                (const supervisor_icmp_ingress_t*)(uintptr_t)arg1,
                (const uint8_t*)(uintptr_t)arg2);
            break;
        case SYS_SCHEDULER_STATS:
            result = (uint32_t)syscall_scheduler_stats(
                (scheduler_resource_stats_t*)(uintptr_t)arg1, arg2, arg3);
            break;
        case SYS_STORAGE_BLOCK_WRITE:
            result = (uint32_t)syscall_storage_block_write(
                arg1, arg2, (const uint8_t*)(uintptr_t)arg3);
            break;
        case SYS_STORAGE_MAINT_ACQUIRE:
            result = (uint32_t)syscall_storage_maintenance_acquire(
                arg1, arg2, (storage_maintenance_token_t*)(uintptr_t)arg3);
            break;
        case SYS_STORAGE_MAINT_RENEW:
            result = (uint32_t)syscall_storage_maintenance_renew(arg1, arg2,
                                                                  arg3);
            break;
        case SYS_STORAGE_MAINT_RELEASE:
            result = (uint32_t)syscall_storage_maintenance_release(arg1, arg2);
            break;
        case SYS_DRIVE_STATUS:
            result = (uint32_t)syscall_drive_status(
                arg1, (void*)(uintptr_t)arg2);
            break;
        case SYS_ADMIN_STORAGE:
            result = (uint32_t)syscall_admin_storage(
                (const admin_storage_request_t*)(uintptr_t)arg1,
                (admin_storage_result_t*)(uintptr_t)arg2);
            break;
        case SYS_COMPONENT_CONTROL:
            result = (uint32_t)syscall_component_control(
                (const component_control_request_t*)(uintptr_t)arg1,
                (component_control_result_t*)(uintptr_t)arg2);
            break;
        case SYS_PARTITION_CREATE:
            result = (uint32_t)syscall_partition_create(
                (const void*)(uintptr_t)arg1);
            break;
        case SYS_STORAGE_BLOCK_FLUSH:
            result = (uint32_t)syscall_storage_block_flush(arg1);
            break;
        case SYS_STORAGE_MEDIA_COMMIT:
            result = (uint32_t)syscall_storage_media_commit(
                arg1, (uint32_t*)(uintptr_t)arg2);
            break;
        case SYS_STORAGE_FORMAT_PROBE:
            result = (uint32_t)syscall_storage_format_probe(arg1, arg2);
            break;
        case SYS_NETWORK_CONTROL:
            result = (uint32_t)syscall_network_control(
                (const network_control_request_t*)(uintptr_t)arg1,
                (network_control_result_t*)(uintptr_t)arg2);
            break;
        case SYS_UDP_SOCKET_CONTROL:
            result = (uint32_t)syscall_udp_socket_control(
                (syscall_udp_socket_control_t*)(uintptr_t)arg1);
            break;
        case SYS_UDP_SOCKET_SENDTO:
            result = (uint32_t)syscall_udp_socket_sendto(
                (const syscall_udp_datagram_t*)(uintptr_t)arg1,
                (const uint8_t*)(uintptr_t)arg2);
            break;
        case SYS_UDP_SOCKET_RECVFROM:
            result = (uint32_t)syscall_udp_socket_recvfrom(
                (syscall_udp_datagram_t*)(uintptr_t)arg1,
                (uint8_t*)(uintptr_t)arg2);
            break;
        case SYS_UDP_SOCKET_INGRESS:
            result = (uint32_t)syscall_udp_socket_ingress(
                (const syscall_udp_datagram_t*)(uintptr_t)arg1,
                (const uint8_t*)(uintptr_t)arg2);
            break;
        case SYS_TCP_SOCKET_CONTROL:
            result = (uint32_t)syscall_tcp_socket_control(
                (syscall_tcp_socket_control_t*)(uintptr_t)arg1);
            break;
        case SYS_TCP_SOCKET_CONNECT:
            result = (uint32_t)syscall_tcp_socket_connect(
                (const syscall_tcp_connect_t*)(uintptr_t)arg1);
            break;
        case SYS_TCP_SOCKET_SEND:
            result = (uint32_t)syscall_tcp_socket_send(
                (const syscall_tcp_io_t*)(uintptr_t)arg1,
                (const uint8_t*)(uintptr_t)arg2);
            break;
        case SYS_TCP_SOCKET_RECEIVE:
            result = (uint32_t)syscall_tcp_socket_receive(
                (syscall_tcp_io_t*)(uintptr_t)arg1,
                (uint8_t*)(uintptr_t)arg2);
            break;
        case SYS_TCP_SOCKET_INGRESS:
            result = (uint32_t)syscall_tcp_socket_ingress(
                (const tcp_socket_segment_t*)(uintptr_t)arg1,
                (const uint8_t*)(uintptr_t)arg2);
            break;
        case SYS_TCP_SOCKET_LISTEN:
            result = (uint32_t)syscall_tcp_socket_listen(
                (const syscall_tcp_listen_t*)(uintptr_t)arg1);
            break;
        case SYS_TCP_SOCKET_ACCEPT:
            result = (uint32_t)syscall_tcp_socket_accept(
                (syscall_tcp_accept_t*)(uintptr_t)arg1);
            break;
        default:
            result = (uint32_t)-1;
            break;
    }

    regs->eax = result;
}
