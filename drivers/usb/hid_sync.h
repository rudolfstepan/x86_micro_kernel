/**
 * @file drivers/usb/hid_sync.h
 * @brief Kurzer SMP-/IRQ-Lock fuer generationsgebundene HID-Zustaende.
 */
#ifndef DRIVERS_USB_HID_SYNC_H
#define DRIVERS_USB_HID_SYNC_H

#include <stdint.h>

#ifdef REIST_USB_HID_HOST_TEST
#include <stdatomic.h>
#include <stdlib.h>

typedef atomic_flag hid_sync_lock_t;
#define HID_SYNC_LOCK_INIT ATOMIC_FLAG_INIT
#define HID_SYNC_HOST_SPIN_LIMIT (1U << 20U)

static inline uint32_t hid_sync_lock_acquire(hid_sync_lock_t *lock) {
    for (uint32_t spin = 0U; spin < HID_SYNC_HOST_SPIN_LIMIT; ++spin) {
        if (!atomic_flag_test_and_set_explicit(lock, memory_order_acquire))
            return 0U;
    }
    abort();
}

static inline void hid_sync_lock_release(hid_sync_lock_t *lock,
                                         uint32_t flags) {
    (void)flags;
    atomic_flag_clear_explicit(lock, memory_order_release);
}
#else
#include "include/lib/spinlock.h"

typedef spinlock_t hid_sync_lock_t;
#define HID_SYNC_LOCK_INIT SPINLOCK_INIT

static inline uint32_t hid_sync_lock_acquire(hid_sync_lock_t *lock) {
    return spinlock_acquire_irq(lock);
}

static inline void hid_sync_lock_release(hid_sync_lock_t *lock,
                                         uint32_t flags) {
    spinlock_release_irq(lock, flags);
}
#endif

#endif
