/**
 * @file drivers/net/tcp_socket.c
 * @brief Bounded TCP active/passive state machine with finite retransmission.
 */
#include "drivers/net/tcp_socket.h"

#include <stdbool.h>

#include "arch/x86/include/interrupt.h"
#include "drivers/net/netstack.h"
#include "kernel/sched/scheduler.h"
#include "kernel/time/pit.h"
#include "include/lib/spinlock.h"
#include "lib/libc/string.h"

#define TCP_RECEIVE_WINDOW TCP_SOCKET_RECEIVE_CAPACITY
#define TCP_INITIAL_RTO_MS 250U
#define TCP_MAX_RETRIES 4U
#define TCP_PASSIVE_HANDSHAKE_MS 5000U
#define TCP_ACCEPT_QUEUE_MS 30000U

typedef struct {
    bool active;
    uint32_t generation;
    int owner_pid;
    uint32_t owner_generation;
    tcp_state_t state;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t send_unacknowledged;
    uint32_t send_next;
    uint32_t receive_next;
    uint16_t peer_window;
    uint16_t receive_head;
    uint16_t receive_count;
    uint16_t backlog;
    /* Zero denotes an independent socket; passive children store the
     * listener's fixed-array index plus one so slot zero remains representable. */
    uint8_t listener_slot;
    /* Ownership transfers to userspace only after accept publishes a process
     * descriptor. Until then the listener closes and deadline sweeps own it. */
    bool accepted;
    uint64_t passive_deadline;
    uint8_t receive_buffer[TCP_SOCKET_RECEIVE_CAPACITY];
    wait_queue_t state_waiters;
    wait_queue_t receive_waiters;
} tcp_control_block_t;

static tcp_control_block_t controls[TCP_SOCKET_MAX_SOCKETS];
static uint32_t dropped_segments;
static uint32_t retransmissions;

typedef struct {
    uint32_t remote_ip;
    uint32_t receive_next;
    uint16_t local_port;
    uint16_t remote_port;
    uint16_t receive_count;
} tcp_wire_context_t;

#ifdef REIST_HOST_TEST
static uint32_t tcp_lock(void) { return 0U; }
static void tcp_unlock(uint32_t flags) { (void)flags; }
#else
static spinlock_t tcp_state_lock = SPINLOCK_INIT;
static uint32_t tcp_lock(void) {
    return spinlock_acquire_irq(&tcp_state_lock);
}
static void tcp_unlock(uint32_t flags) {
    spinlock_release_irq(&tcp_state_lock, flags);
}
#endif

static int tcp_wait_locked(wait_queue_t *queue, uint64_t deadline,
                           uint32_t irq_flags) {
#ifdef REIST_HOST_TEST
    int result = wait_queue_block_until_locked(
        queue, TASK_BLOCK_WAITING, deadline);
    tcp_unlock(irq_flags);
    return result;
#else
    return wait_queue_block_until_spinlocked(
        queue, TASK_BLOCK_WAITING, deadline, &tcp_state_lock, irq_flags);
#endif
}

static uint64_t deadline_after(uint64_t now, uint32_t milliseconds) {
    uint64_t deadline = now + milliseconds;
    return deadline < now ? UINT64_MAX : deadline;
}
static bool sequence_before(uint32_t left, uint32_t right) {
    return (int32_t)(left - right) < 0;
}
static bool sequence_after(uint32_t left, uint32_t right) {
    return sequence_before(right, left);
}
static tcp_socket_handle_t make_handle(uint32_t slot, uint32_t generation) {
    return (generation << 8U) | (slot + 1U);
}
static int resolve(int pid, uint32_t owner_generation,
                   tcp_socket_handle_t handle, tcp_control_block_t **out,
                   uint32_t *slot_out) {
    uint32_t encoded = handle & 0xffU, generation = handle >> 8U;
    if (encoded == 0U || encoded > TCP_SOCKET_MAX_SOCKETS || generation == 0U)
        return -9;
    uint32_t slot = encoded - 1U;
    tcp_control_block_t *control = &controls[slot];
    if (!control->active || control->generation != generation ||
        control->owner_pid != pid ||
        control->owner_generation != owner_generation) return -9;
    *out = control; if (slot_out != NULL) *slot_out = slot; return 0;
}
static void retire_locked(tcp_control_block_t *control) {
    uint32_t generation = control->generation;
    (void)wait_queue_wake_all_locked(&control->state_waiters);
    (void)wait_queue_wake_all_locked(&control->receive_waiters);
    memset(control, 0, sizeof(*control)); control->generation = generation;
}

/* Recycle one fixed slot while advancing its generation, ensuring handles
 * from a retired connection can never address the newly initialized TCB. */
static void initialize_control_locked(tcp_control_block_t *control,
                                      int pid, uint32_t owner_generation) {
    uint32_t generation = control->generation + 1U;
    if (generation == 0U || generation > 0x00ffffffU) generation = 1U;
    memset(control, 0, sizeof(*control));
    control->active = true; control->generation = generation;
    control->owner_pid = pid; control->owner_generation = owner_generation;
    control->state = TCP_CLOSED;
    wait_queue_init(&control->state_waiters);
    wait_queue_init(&control->receive_waiters);
}

/* Bound both incomplete handshakes and established-but-unaccepted children.
 * The caller holds tcp_lock, so reclamation is atomic with lookup/accept. */
static void sweep_passive_locked(uint64_t now) {
    for (uint32_t slot = 0U; slot < TCP_SOCKET_MAX_SOCKETS; ++slot) {
        tcp_control_block_t *control = &controls[slot];
        if (control->active && !control->accepted &&
            control->listener_slot != 0U &&
            control->passive_deadline <= now) retire_locked(control);
    }
}
static tcp_wire_context_t wire_context(const tcp_control_block_t *control) {
    return (tcp_wire_context_t){
        .remote_ip = control->remote_ip, .receive_next = control->receive_next,
        .local_port = control->local_port, .remote_port = control->remote_port,
        .receive_count = control->receive_count,
    };
}
static bool send_control(const tcp_wire_context_t *control, uint32_t sequence,
                         uint8_t flags, const uint8_t *data,
                         uint16_t length) {
    return netstack_send_tcp_segment(
        control->remote_ip, control->local_port, control->remote_port,
        sequence, control->receive_next, flags,
        (uint16_t)(TCP_RECEIVE_WINDOW - control->receive_count), data, length);
}

void tcp_socket_init(void) {
    uint32_t flags = tcp_lock(); memset(controls, 0, sizeof(controls));
    dropped_segments = 0U; retransmissions = 0U; tcp_unlock(flags);
}

int tcp_socket_open(int pid, uint32_t process_generation,
                    tcp_socket_handle_t *handle_out) {
    if (pid <= 0 || process_generation == 0U || handle_out == NULL) return -22;
    uint32_t flags = tcp_lock();
    sweep_passive_locked(pit_monotonic_ms());
    for (uint32_t slot = 0U; slot < TCP_SOCKET_MAX_SOCKETS; ++slot) {
        if (controls[slot].active) continue;
        initialize_control_locked(&controls[slot], pid, process_generation);
        *handle_out = make_handle(slot, controls[slot].generation);
        tcp_unlock(flags); return 0;
    }
    tcp_unlock(flags); return -28;
}

/* Convert an owned closed socket into the sole listener for a local port. */
int tcp_socket_listen(int pid, uint32_t process_generation,
                      tcp_socket_handle_t handle, uint16_t local_port,
                      uint16_t backlog) {
    if (local_port == 0U || backlog == 0U ||
        backlog > TCP_SOCKET_MAX_BACKLOG) return -22;
    uint32_t flags = tcp_lock();
    sweep_passive_locked(pit_monotonic_ms());
    tcp_control_block_t *control = NULL;
    int result = resolve(pid, process_generation, handle, &control, NULL);
    if (result == 0 && control->state != TCP_CLOSED) result = -106;
    for (uint32_t slot = 0U; result == 0 &&
         slot < TCP_SOCKET_MAX_SOCKETS; ++slot) {
        if (&controls[slot] != control && controls[slot].active &&
            controls[slot].state == TCP_LISTEN &&
            controls[slot].local_port == local_port) result = -98;
    }
    if (result == 0) {
        control->local_port = local_port; control->backlog = backlog;
        control->state = TCP_LISTEN;
    }
    tcp_unlock(flags); return result;
}

/* Publish one completed passive child. Waiting is bounded by the caller's
 * deadline and by the earliest pending-child lifetime. */
int tcp_socket_accept(int pid, uint32_t process_generation,
                      tcp_socket_handle_t listener,
                      tcp_socket_handle_t *accepted_out,
                      uint32_t *peer_ip_out, uint16_t *peer_port_out,
                      uint32_t timeout_ms) {
    if (accepted_out == NULL || peer_ip_out == NULL || peer_port_out == NULL ||
        timeout_ms > TCP_SOCKET_MAX_TIMEOUT_MS) return -22;
    uint64_t deadline = deadline_after(pit_monotonic_ms(), timeout_ms);
    for (;;) {
        uint32_t flags = tcp_lock();
        uint64_t now = pit_monotonic_ms(); sweep_passive_locked(now);
        tcp_control_block_t *listener_control = NULL; uint32_t listener_index = 0U;
        int result = resolve(pid, process_generation, listener,
                             &listener_control, &listener_index);
        if (result != 0) { tcp_unlock(flags); return result; }
        if (listener_control->state != TCP_LISTEN) {
            tcp_unlock(flags); return -22;
        }
        uint64_t wait_deadline = deadline;
        for (uint32_t slot = 0U; slot < TCP_SOCKET_MAX_SOCKETS; ++slot) {
            tcp_control_block_t *candidate = &controls[slot];
            if (!candidate->active || candidate->accepted ||
                candidate->listener_slot != listener_index + 1U) continue;
            if (candidate->state == TCP_ESTABLISHED) {
                candidate->accepted = true; candidate->listener_slot = 0U;
                candidate->passive_deadline = 0U;
                *accepted_out = make_handle(slot, candidate->generation);
                *peer_ip_out = candidate->remote_ip;
                *peer_port_out = candidate->remote_port;
                tcp_unlock(flags); return 0;
            }
            if (candidate->passive_deadline < wait_deadline)
                wait_deadline = candidate->passive_deadline;
        }
        if (timeout_ms == 0U) { tcp_unlock(flags); return -11; }
        if (now >= deadline) { tcp_unlock(flags); return -110; }
        result = tcp_wait_locked(&listener_control->state_waiters,
                                 wait_deadline, flags);
        if (result != 0 && result != -110) return -11;
    }
}

static int wait_for_ack(int pid, uint32_t process_generation,
                        tcp_socket_handle_t handle, uint32_t expected_ack,
                        uint32_t sequence, uint8_t flags,
                        const uint8_t *data, uint16_t length,
                        uint64_t deadline) {
    uint32_t retry = 0U, rto = TCP_INITIAL_RTO_MS;
    uint64_t retry_at = pit_monotonic_ms();
    for (;;) {
        uint32_t irq_flags = tcp_lock(); tcp_control_block_t *control = NULL;
        int result = resolve(pid, process_generation, handle, &control, NULL);
        if (result != 0) { tcp_unlock(irq_flags); return result; }
        if (control->state == TCP_CLOSED) { tcp_unlock(irq_flags); return -104; }
        if (control->send_unacknowledged == expected_ack) {
            tcp_unlock(irq_flags); return 0;
        }
        uint64_t now = pit_monotonic_ms();
        if (now >= deadline || retry >= TCP_MAX_RETRIES) {
            control->state = TCP_CLOSED;
            (void)wait_queue_wake_all_locked(&control->receive_waiters);
            tcp_unlock(irq_flags); return -110;
        }
        bool transmit = now >= retry_at;
        uint64_t wait_deadline = transmit ? now : retry_at;
        if (transmit) {
            tcp_wire_context_t snapshot = wire_context(control);
            ++retry; if (retry > 1U && retransmissions != UINT32_MAX)
                ++retransmissions;
            retry_at = deadline_after(now, rto);
            if (retry_at > deadline) retry_at = deadline;
            if (rto < 2000U) rto *= 2U;
            tcp_unlock(irq_flags);
            if (!send_control(&snapshot, sequence, flags, data, length)) {
                if (retry == TCP_MAX_RETRIES) return -113;
            }
            continue;
        }
        if (wait_deadline > deadline) wait_deadline = deadline;
        result = tcp_wait_locked(&control->state_waiters, wait_deadline,
                                 irq_flags);
        if (result != 0 && result != -110) return -11;
    }
}

int tcp_socket_connect(int pid, uint32_t process_generation,
                       tcp_socket_handle_t handle, uint32_t destination_ip,
                       uint16_t destination_port, uint32_t timeout_ms) {
    if (destination_ip == 0U || destination_ip == UINT32_MAX ||
        destination_port == 0U || timeout_ms == 0U ||
        timeout_ms > TCP_SOCKET_MAX_TIMEOUT_MS) return -22;
    uint32_t flags = tcp_lock(); tcp_control_block_t *control = NULL;
    uint32_t slot = 0U;
    int result = resolve(pid, process_generation, handle, &control, &slot);
    if (result == 0 && control->state != TCP_CLOSED) result = -106;
    uint32_t initial = 0U;
    if (result == 0) {
        control->remote_ip = destination_ip;
        control->remote_port = destination_port;
        uint16_t candidate = (uint16_t)(49152U +
            ((slot * 1021U + control->generation) % 8192U));
        bool available = false;
        for (uint32_t attempt = 0U; attempt < TCP_SOCKET_MAX_SOCKETS;
             ++attempt) {
            available = true;
            for (uint32_t other = 0U; other < TCP_SOCKET_MAX_SOCKETS; ++other)
                if (&controls[other] != control && controls[other].active &&
                    controls[other].local_port == candidate) available = false;
            if (available) break;
            candidate = candidate == 57343U ? 49152U
                                             : (uint16_t)(candidate + 1U);
        }
        if (!available) result = -98;
        control->local_port = candidate;
    }
    if (result == 0) {
        initial = (uint32_t)pit_monotonic_ms() ^
                  ((uint32_t)pid << 16U) ^ control->generation;
        if (initial == 0U) initial = 1U;
        control->send_unacknowledged = initial;
        control->send_next = initial + 1U;
        control->peer_window = TCP_SOCKET_MAX_SEGMENT;
        control->state = TCP_SYN_SENT;
    }
    tcp_unlock(flags); if (result != 0) return result;
    uint64_t deadline = deadline_after(pit_monotonic_ms(), timeout_ms);
    result = wait_for_ack(pid, process_generation, handle, initial + 1U,
                          initial, TCP_FLAG_SYN, NULL, 0U, deadline);
    if (result != 0) return result;
    flags = tcp_lock(); control = NULL;
    result = resolve(pid, process_generation, handle, &control, NULL);
    if (result == 0 && control->state != TCP_ESTABLISHED) result = -104;
    tcp_unlock(flags); return result;
}

int tcp_socket_send(int pid, uint32_t process_generation,
                    tcp_socket_handle_t handle, const uint8_t *data,
                    uint32_t length, uint32_t timeout_ms) {
    if (data == NULL || length == 0U || length > TCP_SOCKET_MAX_SEGMENT ||
        timeout_ms == 0U || timeout_ms > TCP_SOCKET_MAX_TIMEOUT_MS) return -22;
    uint32_t flags = tcp_lock(); tcp_control_block_t *control = NULL;
    int result = resolve(pid, process_generation, handle, &control, NULL);
    uint32_t sequence = 0U, expected = 0U;
    if (result == 0 && control->state != TCP_ESTABLISHED) result = -107;
    if (result == 0 && control->send_unacknowledged != control->send_next)
        result = -114;
    if (result == 0 && control->peer_window < length) result = -11;
    if (result == 0) {
        sequence = control->send_next; control->send_next += length;
        expected = control->send_next;
    }
    tcp_unlock(flags); if (result != 0) return result;
    result = wait_for_ack(pid, process_generation, handle, expected, sequence,
                          TCP_FLAG_ACK | TCP_FLAG_PSH, data, (uint16_t)length,
                          deadline_after(pit_monotonic_ms(), timeout_ms));
    return result == 0 ? (int)length : result;
}

int tcp_socket_receive(int pid, uint32_t process_generation,
                       tcp_socket_handle_t handle, uint8_t *data,
                       uint32_t capacity, uint32_t timeout_ms) {
    if (data == NULL || capacity == 0U ||
        capacity > TCP_SOCKET_RECEIVE_CAPACITY ||
        timeout_ms > TCP_SOCKET_MAX_TIMEOUT_MS) return -22;
    uint64_t deadline = deadline_after(pit_monotonic_ms(), timeout_ms);
    for (;;) {
        uint32_t flags = tcp_lock(); tcp_control_block_t *control = NULL;
        int result = resolve(pid, process_generation, handle, &control, NULL);
        if (result != 0) { tcp_unlock(flags); return result; }
        if (control->receive_count != 0U) {
            uint32_t amount = control->receive_count < capacity
                ? control->receive_count : capacity;
            for (uint32_t index = 0U; index < amount; ++index) {
                data[index] = control->receive_buffer[control->receive_head];
                control->receive_head = (uint16_t)((control->receive_head + 1U) %
                    TCP_SOCKET_RECEIVE_CAPACITY);
            }
            control->receive_count = (uint16_t)(control->receive_count - amount);
            tcp_wire_context_t snapshot = wire_context(control);
            uint32_t sequence = control->send_next;
            tcp_unlock(flags);
            /* The ingress ACK advertised the pre-drain window. Publish the
             * reopened fixed receive window immediately; otherwise a peer
             * that reached a zero window has no reason to send again. The
             * update is best-effort and nonblocking because bytes have already
             * been copied to the caller and must not be reported as lost. */
            (void)send_control(&snapshot, sequence, TCP_FLAG_ACK, NULL, 0U);
            return (int)amount;
        }
        if (control->state == TCP_CLOSE_WAIT || control->state == TCP_CLOSED) {
            tcp_unlock(flags); return 0;
        }
        if (timeout_ms == 0U) { tcp_unlock(flags); return -11; }
        if (pit_monotonic_ms() >= deadline) {
            tcp_unlock(flags); return -110;
        }
        result = tcp_wait_locked(&control->receive_waiters, deadline, flags);
        if (result != 0 && result != -110) return -11;
    }
}

int tcp_socket_close(int pid, uint32_t process_generation,
                     tcp_socket_handle_t handle, uint32_t timeout_ms) {
    if (timeout_ms > TCP_SOCKET_MAX_TIMEOUT_MS) return -22;
    uint32_t flags = tcp_lock(); tcp_control_block_t *control = NULL;
    int result = resolve(pid, process_generation, handle, &control, NULL);
    if (result != 0) { tcp_unlock(flags); return result; }
    if (control->state == TCP_LISTEN) {
        /* Closing a listener also revokes every child not yet transferred by
         * accept; already accepted sockets remain independently owned. */
        uint32_t listener_slot = (uint32_t)(control - controls) + 1U;
        for (uint32_t slot = 0U; slot < TCP_SOCKET_MAX_SOCKETS; ++slot) {
            tcp_control_block_t *candidate = &controls[slot];
            if (candidate->active && !candidate->accepted &&
                candidate->listener_slot == listener_slot)
                retire_locked(candidate);
        }
        retire_locked(control); tcp_unlock(flags); return 0;
    }
    if (control->state == TCP_CLOSED || timeout_ms == 0U) {
        bool reset = control->state != TCP_CLOSED;
        tcp_wire_context_t snapshot = wire_context(control);
        uint32_t sequence = control->send_next;
        retire_locked(control); tcp_unlock(flags);
        if (reset) (void)send_control(&snapshot, sequence,
                                     TCP_FLAG_RST | TCP_FLAG_ACK, NULL, 0U);
        return 0;
    }
    if (control->state != TCP_ESTABLISHED && control->state != TCP_CLOSE_WAIT) {
        retire_locked(control); tcp_unlock(flags); return 0;
    }
    bool active_close = control->state == TCP_ESTABLISHED;
    uint8_t close_state = control->state == TCP_CLOSE_WAIT
        ? TCP_LAST_ACK : TCP_FIN_WAIT_1;
    uint32_t sequence = control->send_next++;
    uint32_t expected = control->send_next;
    control->state = (tcp_state_t)close_state;
    tcp_unlock(flags);
    result = wait_for_ack(pid, process_generation, handle, expected, sequence,
                          TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0U,
                          deadline_after(pit_monotonic_ms(), timeout_ms));
    uint64_t close_deadline = deadline_after(pit_monotonic_ms(), timeout_ms);
    while (result == 0 && active_close) {
        flags = tcp_lock(); control = NULL;
        result = resolve(pid, process_generation, handle, &control, NULL);
        if (result != 0) { tcp_unlock(flags); break; }
        if (control->state == TCP_TIME_WAIT || control->state == TCP_CLOSED) {
            tcp_unlock(flags); result = 0; break;
        }
        if (pit_monotonic_ms() >= close_deadline) {
            tcp_unlock(flags); result = -110; break;
        }
        int wait_result = tcp_wait_locked(&control->state_waiters,
                                          close_deadline, flags);
        if (wait_result != 0 && wait_result != -110) {
            result = -11; break;
        }
    }
    flags = tcp_lock(); control = NULL;
    int resolved = resolve(pid, process_generation, handle, &control, NULL);
    if (resolved == 0) retire_locked(control);
    tcp_unlock(flags); return result == -104 ? 0 : result;
}

void tcp_socket_process_cleanup(int pid, uint32_t process_generation) {
    tcp_wire_context_t reset_contexts[TCP_SOCKET_MAX_SOCKETS];
    uint32_t reset_sequences[TCP_SOCKET_MAX_SOCKETS];
    uint32_t reset_count = 0U;
    uint32_t flags = tcp_lock();
    for (uint32_t slot = 0U; slot < TCP_SOCKET_MAX_SOCKETS; ++slot) {
        tcp_control_block_t *control = &controls[slot];
        if (control->active && control->owner_pid == pid &&
            control->owner_generation == process_generation) {
            if (control->state != TCP_CLOSED) {
                reset_contexts[reset_count] = wire_context(control);
                reset_sequences[reset_count++] = control->send_next;
            }
            retire_locked(control);
        }
    }
    tcp_unlock(flags);
    for (uint32_t index = 0U; index < reset_count; ++index)
        (void)send_control(&reset_contexts[index], reset_sequences[index],
                           TCP_FLAG_RST | TCP_FLAG_ACK, NULL, 0U);
}

int tcp_socket_ingress(const tcp_socket_segment_t *segment,
                       const uint8_t *data) {
    if (segment == NULL || segment->version != TCP_SOCKET_ABI_VERSION ||
        segment->struct_size != sizeof(*segment) || segment->source_ip == 0U ||
        segment->destination_ip != netstack_get_ip_address() ||
        segment->source_port == 0U || segment->destination_port == 0U ||
        segment->length > TCP_SOCKET_MAX_SEGMENT ||
        (segment->length != 0U && data == NULL)) return -22;
    for (uint32_t index = 0U; index < sizeof(segment->reserved); ++index)
        if (segment->reserved[index] != 0U) return -22;
    uint32_t flags = tcp_lock();
    uint64_t now = pit_monotonic_ms(); sweep_passive_locked(now);
    tcp_control_block_t *control = NULL;
    for (uint32_t slot = 0U; slot < TCP_SOCKET_MAX_SOCKETS; ++slot) {
        tcp_control_block_t *candidate = &controls[slot];
        if (candidate->active && candidate->remote_ip == segment->source_ip &&
            candidate->local_port == segment->destination_port &&
            candidate->remote_port == segment->source_port) {
            control = candidate; break;
        }
    }
    if (control == NULL) {
        /* No established tuple matched. Only a bare SYN may allocate one
         * fixed passive child, and backlog/capacity are checked first. */
        tcp_control_block_t *listener = NULL; uint32_t listener_index = 0U;
        for (uint32_t slot = 0U; slot < TCP_SOCKET_MAX_SOCKETS; ++slot) {
            if (controls[slot].active && controls[slot].state == TCP_LISTEN &&
                controls[slot].local_port == segment->destination_port) {
                listener = &controls[slot]; listener_index = slot; break;
            }
        }
        if (listener == NULL) { tcp_unlock(flags); return -2; }
        if ((segment->flags & (TCP_FLAG_SYN | TCP_FLAG_ACK | TCP_FLAG_RST |
                               TCP_FLAG_FIN)) != TCP_FLAG_SYN ||
            segment->length != 0U) {
            if (dropped_segments != UINT32_MAX) ++dropped_segments;
            tcp_unlock(flags); return -84;
        }
        uint32_t pending = 0U, child_index = TCP_SOCKET_MAX_SOCKETS;
        for (uint32_t slot = 0U; slot < TCP_SOCKET_MAX_SOCKETS; ++slot) {
            if (controls[slot].active && !controls[slot].accepted &&
                controls[slot].listener_slot == listener_index + 1U) ++pending;
            if (!controls[slot].active && child_index == TCP_SOCKET_MAX_SOCKETS)
                child_index = slot;
        }
        if (pending >= listener->backlog ||
            child_index == TCP_SOCKET_MAX_SOCKETS) {
            if (dropped_segments != UINT32_MAX) ++dropped_segments;
            tcp_unlock(flags); return -105;
        }
        tcp_control_block_t *child = &controls[child_index];
        int owner_pid = listener->owner_pid;
        uint32_t owner_generation = listener->owner_generation;
        initialize_control_locked(child, owner_pid, owner_generation);
        child->state = TCP_SYN_RECEIVED;
        child->remote_ip = segment->source_ip;
        child->remote_port = segment->source_port;
        child->local_port = segment->destination_port;
        child->receive_next = segment->sequence + 1U;
        child->peer_window = segment->window;
        child->listener_slot = (uint8_t)(listener_index + 1U);
        child->passive_deadline = deadline_after(now, TCP_PASSIVE_HANDSHAKE_MS);
        uint32_t initial = (uint32_t)now ^ segment->source_ip ^
                           ((uint32_t)segment->source_port << 16U) ^
                           child->generation;
        if (initial == 0U) initial = 1U;
        child->send_unacknowledged = initial;
        child->send_next = initial + 1U;
        tcp_wire_context_t snapshot = wire_context(child);
        tcp_socket_handle_t child_handle = make_handle(
            child_index, child->generation);
        tcp_unlock(flags);
        if (send_control(&snapshot, initial, TCP_FLAG_SYN | TCP_FLAG_ACK,
                         NULL, 0U)) return 0;
        flags = tcp_lock(); child = NULL;
        if (resolve(owner_pid, owner_generation, child_handle,
                    &child, NULL) == 0 &&
            child->state == TCP_SYN_RECEIVED) retire_locked(child);
        tcp_unlock(flags); return -11;
    }
    bool send_ack = false;
    if ((segment->flags & TCP_FLAG_RST) != 0U) {
        control->state = TCP_CLOSED;
        (void)wait_queue_wake_all_locked(&control->state_waiters);
        (void)wait_queue_wake_all_locked(&control->receive_waiters);
        tcp_unlock(flags); return 0;
    }
    if (control->state == TCP_SYN_SENT) {
        if ((segment->flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) !=
                (TCP_FLAG_SYN | TCP_FLAG_ACK) ||
            segment->acknowledgement != control->send_next ||
            segment->length != 0U) {
            if (dropped_segments != UINT32_MAX) ++dropped_segments;
            tcp_unlock(flags); return -84;
        }
        control->send_unacknowledged = segment->acknowledgement;
        control->receive_next = segment->sequence + 1U;
        control->peer_window = segment->window;
        control->state = TCP_ESTABLISHED; send_ack = true;
        (void)wait_queue_wake_all_locked(&control->state_waiters);
    } else if (control->state == TCP_SYN_RECEIVED) {
        /* A duplicate SYN retransmits the existing SYN/ACK without allocating
         * another child or changing the initial sequence number. */
        if ((segment->flags & (TCP_FLAG_SYN | TCP_FLAG_ACK | TCP_FLAG_RST |
                               TCP_FLAG_FIN)) == TCP_FLAG_SYN &&
            segment->sequence + 1U == control->receive_next &&
            segment->length == 0U) {
            tcp_wire_context_t snapshot = wire_context(control);
            uint32_t sequence = control->send_unacknowledged;
            tcp_unlock(flags);
            return send_control(&snapshot, sequence,
                                TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0U)
                ? 0 : -11;
        }
        if ((segment->flags & (TCP_FLAG_SYN | TCP_FLAG_ACK | TCP_FLAG_RST |
                               TCP_FLAG_FIN)) != TCP_FLAG_ACK ||
            segment->sequence != control->receive_next ||
            segment->acknowledgement != control->send_next) {
            if (dropped_segments != UINT32_MAX) ++dropped_segments;
            tcp_unlock(flags); return -84;
        }
        control->send_unacknowledged = segment->acknowledgement;
        control->peer_window = segment->window;
        control->state = TCP_ESTABLISHED;
        control->passive_deadline = deadline_after(now, TCP_ACCEPT_QUEUE_MS);
        if (control->listener_slot != 0U) {
            tcp_control_block_t *listener =
                &controls[control->listener_slot - 1U];
            if (listener->active && listener->state == TCP_LISTEN)
                (void)wait_queue_wake_all_locked(&listener->state_waiters);
        }
        (void)wait_queue_wake_all_locked(&control->state_waiters);
        /* The final handshake ACK may legally carry the first application
         * bytes.  Preserve them in the bounded receive queue before waking
         * accept/receive instead of requiring a separate empty ACK frame. */
        if (segment->length != 0U) {
            uint32_t tail = (control->receive_head + control->receive_count) %
                            TCP_SOCKET_RECEIVE_CAPACITY;
            for (uint32_t index = 0U; index < segment->length; ++index) {
                control->receive_buffer[tail] = data[index];
                tail = (tail + 1U) % TCP_SOCKET_RECEIVE_CAPACITY;
            }
            control->receive_count = (uint16_t)(
                control->receive_count + segment->length);
            control->receive_next += segment->length;
            send_ack = true;
            (void)wait_queue_wake_all_locked(&control->receive_waiters);
        }
    } else {
        if ((segment->flags & TCP_FLAG_ACK) != 0U) {
            if (sequence_before(segment->acknowledgement,
                                control->send_unacknowledged) ||
                sequence_after(segment->acknowledgement,
                               control->send_next)) {
                if (dropped_segments != UINT32_MAX) ++dropped_segments;
                tcp_unlock(flags); return -84;
            }
            if (sequence_after(segment->acknowledgement,
                               control->send_unacknowledged)) {
                control->send_unacknowledged = segment->acknowledgement;
                control->peer_window = segment->window;
                if (control->state == TCP_FIN_WAIT_1 &&
                    control->send_unacknowledged == control->send_next)
                    control->state = TCP_FIN_WAIT_2;
                if (control->state == TCP_LAST_ACK &&
                    control->send_unacknowledged == control->send_next)
                    control->state = TCP_CLOSED;
                (void)wait_queue_wake_all_locked(&control->state_waiters);
            }
        }
        if (segment->length != 0U) {
            if (segment->sequence != control->receive_next ||
                segment->length > TCP_SOCKET_RECEIVE_CAPACITY -
                                  control->receive_count) {
                if (dropped_segments != UINT32_MAX) ++dropped_segments;
                send_ack = true;
            } else {
                uint32_t tail = (control->receive_head + control->receive_count) %
                                TCP_SOCKET_RECEIVE_CAPACITY;
                for (uint32_t index = 0U; index < segment->length; ++index) {
                    control->receive_buffer[tail] = data[index];
                    tail = (tail + 1U) % TCP_SOCKET_RECEIVE_CAPACITY;
                }
                control->receive_count = (uint16_t)(
                    control->receive_count + segment->length);
                control->receive_next += segment->length; send_ack = true;
                (void)wait_queue_wake_all_locked(&control->receive_waiters);
            }
        }
        if ((segment->flags & TCP_FLAG_FIN) != 0U &&
            segment->sequence + segment->length == control->receive_next) {
            ++control->receive_next; send_ack = true;
            control->state = control->state == TCP_FIN_WAIT_2
                ? TCP_TIME_WAIT : TCP_CLOSE_WAIT;
            (void)wait_queue_wake_all_locked(&control->receive_waiters);
            (void)wait_queue_wake_all_locked(&control->state_waiters);
        }
    }
    tcp_wire_context_t snapshot = wire_context(control);
    uint32_t send_next = control->send_next;
    tcp_unlock(flags);
    if (send_ack && !send_control(&snapshot, send_next,
                                  TCP_FLAG_ACK, NULL, 0U)) return -11;
    return 0;
}

void tcp_socket_get_stats(tcp_socket_stats_t *stats_out) {
    if (stats_out == NULL) return;
    tcp_socket_stats_t stats = {
        .version = TCP_SOCKET_ABI_VERSION, .struct_size = sizeof(stats),
    };
    uint32_t flags = tcp_lock();
    for (uint32_t slot = 0U; slot < TCP_SOCKET_MAX_SOCKETS; ++slot) {
        if (!controls[slot].active) continue;
        ++stats.active_sockets; stats.received_bytes += controls[slot].receive_count;
        if (controls[slot].state == TCP_ESTABLISHED)
            ++stats.established_sockets;
    }
    stats.dropped_segments = dropped_segments;
    stats.retransmissions = retransmissions; tcp_unlock(flags);
    *stats_out = stats;
}
