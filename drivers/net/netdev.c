#include "netdev.h"

#include "drivers/net/e1000.h"
#include "drivers/net/ne2000.h"
#include "drivers/net/rtl8139.h"
#include "include/kernel/panic.h"
#include "include/kernel/supervisor.h"
#include "kernel/time/pit.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"

#define NETDEV_MONITOR_QUEUE_SIZE 8
#define NETDEV_SERVICE_QUEUE_SIZE 8
#define NETDEV_MAX_FRAME_SIZE 1518

typedef struct {
    uint16_t length;
    uint8_t data[NETDEV_MAX_FRAME_SIZE];
} netdev_queued_packet_t;

static netdev_queued_packet_t monitor_queue[NETDEV_MONITOR_QUEUE_SIZE];
static volatile uint8_t monitor_queue_head;
static volatile uint8_t monitor_queue_tail;
static netdev_queued_packet_t service_queue[NETDEV_SERVICE_QUEUE_SIZE];
static volatile uint8_t service_queue_head;
static volatile uint8_t service_queue_tail;
static volatile uint32_t rx_producer_busy;
static volatile uint32_t netdev_poll_busy;
static volatile bool netdev_tx_fenced;
static bool netdev_supervised;
static uint64_t netdev_progress_marker;
static supervisor_handle_t netdev_supervisor_handle;

#define NETDEV_TX_DEADLINE_MS 250U

static bool netdev_apply_supervisor_fence(void *context) {
    (void)context;
    netdev_fence_outputs();
    return true;
}

static bool netdev_verify_supervisor_fence(void *context) {
    (void)context;
    return netdev_outputs_fenced();
}

bool netdev_supervision_init(uint64_t now_ms) {
    if (netdev_supervised) return true;
    supervisor_config_t config = {
        .heartbeat_timeout_ms = NETDEV_TX_DEADLINE_MS,
        .recovery_timeout_ms = NETDEV_TX_DEADLINE_MS,
        .restart_budget = 0,
    };
    supervisor_fence_ops_t fence_ops = {
        .apply = netdev_apply_supervisor_fence,
        .verify = netdev_verify_supervisor_fence,
        .context = NULL,
    };
    if (supervisor_register("network-tx", &config, &fence_ops, now_ms,
                            &netdev_supervisor_handle) != 0 ||
        supervisor_report_progress(netdev_supervisor_handle, 1U, now_ms) != 0 ||
        supervisor_report_idle(netdev_supervisor_handle) != 0) return false;
    netdev_progress_marker = 1U;
    netdev_supervised = true;
    return true;
}

bool netdev_available(void) {
    return e1000_is_initialized() || rtl8139_is_initialized() ||
           ne2000_is_initialized();
}

const char* netdev_backend_name(void) {
    if (e1000_is_initialized()) return "E1000";
    if (rtl8139_is_initialized()) return "RTL8139";
    if (ne2000_is_initialized()) return "NE2000";
    return "none";
}

bool netdev_send(const uint8_t* packet, size_t length) {
    if (netdev_tx_fenced) return false;
    if (!packet || length < 14u || length > NETDEV_MAX_FRAME_SIZE) return false;
    if (netdev_supervised &&
        supervisor_report_progress(netdev_supervisor_handle,
                                   ++netdev_progress_marker,
                                   pit_monotonic_ms()) != 0) return false;
    bool result = false;
    if (e1000_is_initialized()) {
        result = e1000_send_packet((void*)packet, length);
    } else if (rtl8139_is_initialized()) {
        result = rtl8139_send_packet((void*)packet, (uint16_t)length);
    } else if (ne2000_is_initialized()) {
        result = ne2000_send_packet((uint8_t*)packet, (uint16_t)length);
    }
    if (netdev_supervised && supervisor_report_idle(netdev_supervisor_handle) != 0)
        return false;
    return result;
}

void netdev_fence_outputs(void) {
    /* Publish the software interlock before touching fallible hardware. */
    netdev_tx_fenced = true;
    __asm__ volatile("" ::: "memory");
    e1000_fence_outputs();
    rtl8139_fence_outputs();
    ne2000_fence_outputs();
}

bool netdev_outputs_fenced(void) {
    return netdev_tx_fenced && e1000_outputs_fenced() &&
           rtl8139_outputs_fenced() && ne2000_outputs_fenced();
}

bool netdev_get_mac_address(uint8_t mac[6]) {
    if (!mac) return false;
    if (e1000_is_initialized()) e1000_get_mac_address(mac);
    else if (rtl8139_is_initialized()) rtl8139_get_mac_address(mac);
    else if (ne2000_is_initialized()) ne2000_get_mac_address(mac);
    else {
        memset(mac, 0, 6);
        return false;
    }
    return true;
}

static void netdev_queue_monitor_packet(const uint8_t* packet,
                                        uint16_t length) {
    uint8_t head = monitor_queue_head;
    uint8_t next =
        (uint8_t)((head + 1u) % NETDEV_MONITOR_QUEUE_SIZE);
    if (next == monitor_queue_tail) return;
    monitor_queue[head].length = length;
    memcpy(monitor_queue[head].data, packet, length);
    __asm__ volatile("" ::: "memory");
    monitor_queue_head = next;
}

static void netdev_queue_service_packet(const uint8_t* packet,
                                        uint16_t length) {
    uint8_t head = service_queue_head;
    uint8_t next =
        (uint8_t)((head + 1u) % NETDEV_SERVICE_QUEUE_SIZE);
    if (next == service_queue_tail) return;
    service_queue[head].length = length;
    memcpy(service_queue[head].data, packet, length);
    __asm__ volatile("" ::: "memory");
    service_queue_head = next;
}

void netdev_deliver_rx(const uint8_t* packet, uint16_t length) {
    if (!packet || length < 14u || length > NETDEV_MAX_FRAME_SIZE) return;
    /* IRQ producers cannot nest on this UP kernel, but the deferred NE2000
     * drain runs with interrupts enabled.  Drop rather than spin if an IRQ
     * interrupts that foreground producer. */
    if (__sync_lock_test_and_set(&rx_producer_busy, 1u)) return;
    if (length >= 42U) {
        uint8_t service_header[42U];
        memcpy(service_header, packet, sizeof(service_header));
        (void)supervisor_network_submit_header(service_header,
                                               sizeof(service_header));
    }
    /* Every ingress decision belongs to the bounded Ring-3 service queue.
     * The monitor copy is diagnostic only and grants no network authority. */
    netdev_queue_monitor_packet(packet, length);
    netdev_queue_service_packet(packet, length);
    __sync_lock_release(&rx_producer_busy);
}

void netdev_poll(void) {
    KASSERT_NOT_IRQ();
    KASSERT(irq_enabled());
    /* Multiple foreground callers may be preempted between polling passes.
     * Never spin behind the parked task; the pending device flag will make a
     * later pass retry the work. */
    if (__sync_lock_test_and_set(&netdev_poll_busy, 1u)) return;
    if (e1000_is_initialized()) e1000_poll_rx();
    if (rtl8139_is_initialized()) rtl8139_poll_rx();
    if (ne2000_is_initialized()) ne2000_poll_rx();
    __sync_lock_release(&netdev_poll_busy);
}

int netdev_receive_frame(uint8_t* buffer, size_t capacity) {
    uint8_t tail = monitor_queue_tail;
    if (tail == monitor_queue_head) return 0;

    uint16_t length = monitor_queue[tail].length;
    uint8_t next =
        (uint8_t)((tail + 1u) % NETDEV_MONITOR_QUEUE_SIZE);
    if (!buffer || length > capacity) {
        monitor_queue_tail = next;
        return -1;
    }
    memcpy(buffer, monitor_queue[tail].data, length);
    __asm__ volatile("" ::: "memory");
    monitor_queue_tail = next;
    return (int)length;
}

int netdev_receive_service_frame(uint8_t* buffer, size_t capacity) {
    uint8_t tail = service_queue_tail;
    uint8_t head = service_queue_head;
    if (tail >= NETDEV_SERVICE_QUEUE_SIZE || head >= NETDEV_SERVICE_QUEUE_SIZE) {
        service_queue_tail = 0U;
        service_queue_head = 0U;
        return -1;
    }
    if (tail == head) return 0;

    uint16_t length = service_queue[tail].length;
    uint8_t next =
        (uint8_t)((tail + 1u) % NETDEV_SERVICE_QUEUE_SIZE);
    if (!buffer || length < 14U || length > NETDEV_MAX_FRAME_SIZE ||
        length > capacity) {
        service_queue_tail = next;
        return -1;
    }
    memcpy(buffer, service_queue[tail].data, length);
    __asm__ volatile("" ::: "memory");
    service_queue_tail = next;
    return (int)length;
}

void netdev_reset_monitor(void) {
    monitor_queue_tail = monitor_queue_head;
}

void netdev_reset_service_frames(void) {
    service_queue_tail = service_queue_head;
}
