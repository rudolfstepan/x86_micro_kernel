/**
 * @file kernel/init/watchdog.c
 * @brief Koppelt Hardware-Watchdog-Bedienung an nachgewiesenen Systemfortschritt.
 *
 * Layer: Ring-0 liveness monitor.
 * Contract: Ticks und Fortschrittsmarker verwenden monotone Zeit und feste Intervalle.
 * Safety: Stillstand führt zur unterlassenen Bedienung oder zum Fatal-Handoff.
 */
#include "include/kernel/watchdog.h"

#include "arch/x86/include/interrupt.h"
#include "drivers/char/io.h"

#define IB700_DISABLE_PORT 0x441U
#define IB700_ENABLE_PORT  0x443U
#define IB700_TIMEOUT_4_SECONDS 13U
#define IB700_TIMEOUT_2_SECONDS 14U
#define WATCHDOG_FEED_INTERVAL_MS 1000U

static volatile uint32_t health_epoch;
static uint32_t observed_epoch;
static uint64_t last_feed_ms;
static bool backend_available;

void watchdog_init(void) {
#ifdef QEMU_BUILD
    /* QEMU's explicit -device ib700 backend is the qualified emulator
     * profile. ISA has no discovery handshake, so availability is a build and
     * launch contract verified by the watchdog smoke target. */
    backend_available = true;
    observed_epoch = health_epoch;
    last_feed_ms = 0;
    outb(IB700_ENABLE_PORT, IB700_TIMEOUT_4_SECONDS);
#else
    backend_available = false;
#endif
}

void watchdog_health_progress(void) {
    ++health_epoch;
}

void watchdog_clock_tick(uint64_t monotonic_ms) {
    if (!backend_available ||
        monotonic_ms - last_feed_ms < WATCHDOG_FEED_INTERVAL_MS) return;
    last_feed_ms = monotonic_ms;
    uint32_t current = health_epoch;
    if (current == observed_epoch) return;
    observed_epoch = current;
    outb(IB700_ENABLE_PORT, IB700_TIMEOUT_4_SECONDS);
}

bool watchdog_fatal_handoff(void) {
    if (!backend_available) return false;
    /* Arm the shortest backend interval and never feed it again. */
    outb(IB700_ENABLE_PORT, IB700_TIMEOUT_2_SECONDS);
    backend_available = false;
    return true;
}

bool watchdog_available(void) {
    uint32_t flags = irq_save();
    bool available = backend_available;
    irq_restore(flags);
    return available;
}
