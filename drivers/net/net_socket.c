/**
 * @file drivers/net/net_socket.c
 * @brief Implements bounded generation-scoped IPv4/UDP sockets.
 */
#include "drivers/net/net_socket.h"

#include <stdbool.h>

#include "arch/x86/include/interrupt.h"
#include "drivers/net/netstack.h"
#include "kernel/sched/scheduler.h"
#include "kernel/time/pit.h"
#include "include/lib/spinlock.h"
#include "lib/libc/string.h"

typedef struct {
    net_socket_datagram_t metadata;
    uint8_t data[NET_SOCKET_MAX_DATAGRAM];
} queued_datagram_t;

typedef struct {
    bool active;
    uint32_t generation;
    int owner_pid;
    uint32_t owner_generation;
    uint16_t local_port;
    uint8_t head;
    uint8_t count;
    wait_queue_t receive_waiters;
    queued_datagram_t queue[NET_SOCKET_QUEUE_DEPTH];
} udp_socket_t;

static udp_socket_t sockets[NET_SOCKET_MAX_SOCKETS];
static uint32_t dropped_datagrams;

#ifdef REIST_HOST_TEST
static uint32_t socket_lock(void) { return 0U; }
static void socket_unlock(uint32_t flags) { (void)flags; }
#else
static spinlock_t socket_state_lock = SPINLOCK_INIT;
static uint32_t socket_lock(void) {
    return spinlock_acquire_irq(&socket_state_lock);
}
static void socket_unlock(uint32_t flags) {
    spinlock_release_irq(&socket_state_lock, flags);
}
#endif

static net_socket_handle_t make_handle(uint32_t slot, uint32_t generation) {
    return (generation << 8U) | (slot + 1U);
}

static int resolve(int pid, uint32_t owner_generation,
                   net_socket_handle_t handle, udp_socket_t **socket_out,
                   uint32_t *slot_out) {
    uint32_t encoded = handle & 0xffU;
    uint32_t generation = handle >> 8U;
    if (encoded == 0U || encoded > NET_SOCKET_MAX_SOCKETS || generation == 0U)
        return -9;
    uint32_t slot = encoded - 1U;
    udp_socket_t *socket = &sockets[slot];
    if (!socket->active || socket->generation != generation ||
        socket->owner_pid != pid || socket->owner_generation != owner_generation)
        return -9;
    *socket_out = socket;
    if (slot_out != NULL) *slot_out = slot;
    return 0;
}

void net_socket_init(void) {
    uint32_t flags = socket_lock();
    memset(sockets, 0, sizeof(sockets));
    dropped_datagrams = 0U;
    socket_unlock(flags);
}

int net_socket_open(int pid, uint32_t process_generation,
                    net_socket_handle_t *handle_out) {
    if (pid <= 0 || process_generation == 0U || handle_out == NULL) return -22;
    uint32_t flags = socket_lock();
    for (uint32_t slot = 0U; slot < NET_SOCKET_MAX_SOCKETS; ++slot) {
        if (sockets[slot].active) continue;
        uint32_t generation = sockets[slot].generation + 1U;
        if (generation == 0U || generation > 0x00ffffffU) generation = 1U;
        memset(&sockets[slot], 0, sizeof(sockets[slot]));
        sockets[slot].active = true;
        sockets[slot].generation = generation;
        sockets[slot].owner_pid = pid;
        sockets[slot].owner_generation = process_generation;
        wait_queue_init(&sockets[slot].receive_waiters);
        *handle_out = make_handle(slot, generation);
        socket_unlock(flags);
        return 0;
    }
    socket_unlock(flags);
    return -28;
}

int net_socket_bind(int pid, uint32_t process_generation,
                    net_socket_handle_t handle, uint16_t port) {
    if (port == 0U) return -22;
    uint32_t flags = socket_lock();
    udp_socket_t *socket = NULL;
    int result = resolve(pid, process_generation, handle, &socket, NULL);
    for (uint32_t slot = 0U; result == 0 && slot < NET_SOCKET_MAX_SOCKETS;
         ++slot) {
        if (&sockets[slot] != socket && sockets[slot].active &&
            sockets[slot].local_port == port) result = -98;
    }
    if (result == 0 && socket->local_port != 0U && socket->local_port != port)
        result = -22;
    if (result == 0) socket->local_port = port;
    socket_unlock(flags);
    return result;
}

int net_socket_sendto(int pid, uint32_t process_generation,
                      net_socket_handle_t handle, uint32_t destination_ip,
                      uint16_t destination_port, const uint8_t *data,
                      uint32_t length, uint32_t timeout_ms) {
    if (destination_ip == 0U || destination_port == 0U ||
        length > NET_SOCKET_MAX_DATAGRAM || timeout_ms > 10000U ||
        (length != 0U && data == NULL))
        return -22;
    uint16_t source_port = 0U;
    uint32_t flags = socket_lock();
    udp_socket_t *socket = NULL;
    int result = resolve(pid, process_generation, handle, &socket, NULL);
    if (result == 0) source_port = socket->local_port;
    socket_unlock(flags);
    if (result != 0) return result;
    if (source_port == 0U) return -89;
    uint64_t now = pit_monotonic_ms();
    uint64_t deadline = now + (uint64_t)timeout_ms;
    if (deadline < now) deadline = UINT64_MAX;
    for (;;) {
        result = udp_send(destination_ip, source_port, destination_port,
                          data, (uint16_t)length);
        if (result == 0) return (int)length;
        if (timeout_ms == 0U) return -11;
        now = pit_monotonic_ms();
        if (now >= deadline) return -110;
        uint64_t remaining = deadline - now;
        uint32_t wait_ms = remaining < 20U ? (uint32_t)remaining : 20U;
        if (scheduler_sleep_ms(wait_ms) != 0) (void)scheduler_yield();
    }
}

int net_socket_recvfrom(int pid, uint32_t process_generation,
                        net_socket_handle_t handle,
                        net_socket_datagram_t *datagram_out, uint8_t *data,
                        uint32_t capacity, uint32_t timeout_ms) {
    if (datagram_out == NULL || (capacity != 0U && data == NULL)) return -22;
    uint64_t now = pit_monotonic_ms();
    uint64_t deadline = now + (uint64_t)timeout_ms;
    if (deadline < now) deadline = UINT64_MAX;
    for (;;) {
        uint32_t flags = socket_lock();
        udp_socket_t *socket = NULL;
        int result = resolve(pid, process_generation, handle, &socket, NULL);
        if (result != 0) { socket_unlock(flags); return result; }
        if (socket->count == 0U) {
            if (timeout_ms == 0U) { socket_unlock(flags); return -11; }
            if (pit_monotonic_ms() >= deadline) {
                socket_unlock(flags); return -110;
            }
#ifdef REIST_HOST_TEST
            result = wait_queue_block_until_locked(
                &socket->receive_waiters, TASK_BLOCK_WAITING, deadline);
            socket_unlock(flags);
#else
            result = wait_queue_block_until_spinlocked(
                &socket->receive_waiters, TASK_BLOCK_WAITING, deadline,
                &socket_state_lock, flags);
#endif
            if (result == -110) return -110;
            if (result != 0) return -11;
            continue;
        }
        queued_datagram_t *queued = &socket->queue[socket->head];
        if (capacity < queued->metadata.length) {
            socket_unlock(flags); return -90;
        }
        else {
            *datagram_out = queued->metadata;
            if (queued->metadata.length != 0U)
                memcpy(data, queued->data, queued->metadata.length);
            memset(queued, 0, sizeof(*queued));
            socket->head = (uint8_t)((socket->head + 1U) %
                                     NET_SOCKET_QUEUE_DEPTH);
            --socket->count;
            result = (int)datagram_out->length;
        }
        socket_unlock(flags);
        return result;
    }
}

int net_socket_close(int pid, uint32_t process_generation,
                     net_socket_handle_t handle) {
    uint32_t flags = socket_lock();
    udp_socket_t *socket = NULL;
    int result = resolve(pid, process_generation, handle, &socket, NULL);
    if (result == 0) {
        uint32_t generation = socket->generation;
        (void)wait_queue_wake_all_locked(&socket->receive_waiters);
        memset(socket, 0, sizeof(*socket));
        socket->generation = generation;
    }
    socket_unlock(flags);
    return result;
}

void net_socket_process_cleanup(int pid, uint32_t process_generation) {
    uint32_t flags = socket_lock();
    for (uint32_t slot = 0U; slot < NET_SOCKET_MAX_SOCKETS; ++slot) {
        udp_socket_t *socket = &sockets[slot];
        if (socket->active && socket->owner_pid == pid &&
            socket->owner_generation == process_generation) {
            uint32_t generation = socket->generation;
            (void)wait_queue_wake_all_locked(&socket->receive_waiters);
            memset(socket, 0, sizeof(*socket));
            socket->generation = generation;
        }
    }
    socket_unlock(flags);
}

int net_socket_ingress(uint32_t source_ip, uint16_t source_port,
                       uint16_t destination_port, const uint8_t *data,
                       uint32_t length) {
    if (source_ip == 0U || source_port == 0U || destination_port == 0U ||
        length > NET_SOCKET_MAX_DATAGRAM || (length != 0U && data == NULL))
        return -22;
    uint32_t flags = socket_lock();
    for (uint32_t slot = 0U; slot < NET_SOCKET_MAX_SOCKETS; ++slot) {
        udp_socket_t *socket = &sockets[slot];
        if (!socket->active || socket->local_port != destination_port) continue;
        if (socket->count >= NET_SOCKET_QUEUE_DEPTH) {
            if (dropped_datagrams != UINT32_MAX) ++dropped_datagrams;
            socket_unlock(flags); return -105;
        }
        uint32_t tail = (socket->head + socket->count) % NET_SOCKET_QUEUE_DEPTH;
        queued_datagram_t *queued = &socket->queue[tail];
        memset(queued, 0, sizeof(*queued));
        queued->metadata = (net_socket_datagram_t){
            .version = NET_SOCKET_ABI_VERSION,
            .struct_size = sizeof(net_socket_datagram_t),
            .source_ip = source_ip, .source_port = source_port,
            .destination_port = destination_port, .length = length,
        };
        if (length != 0U) memcpy(queued->data, data, length);
        ++socket->count;
        (void)wait_queue_wake_one_locked(&socket->receive_waiters);
        socket_unlock(flags); return 0;
    }
    socket_unlock(flags);
    return -2;
}

void net_socket_get_stats(net_socket_stats_t *stats_out) {
    if (stats_out == NULL) return;
    net_socket_stats_t stats = {
        .version = NET_SOCKET_ABI_VERSION,
        .struct_size = sizeof(stats), .dropped_datagrams = dropped_datagrams,
    };
    uint32_t flags = socket_lock();
    for (uint32_t slot = 0U; slot < NET_SOCKET_MAX_SOCKETS; ++slot) {
        if (!sockets[slot].active) continue;
        ++stats.active_sockets;
        stats.queued_datagrams += sockets[slot].count;
    }
    stats.dropped_datagrams = dropped_datagrams;
    socket_unlock(flags);
    *stats_out = stats;
}
