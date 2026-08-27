/**
 * @file kernel/time/apic.c
 * @brief Lokaler APIC-Timer und Kalibrierung.
 *
 * Layer: Ring-0 x86 time backend.
 * Contract: Erfolgreiche Initialisierung liefert monotone, begrenzte Timerdienste.
 * Safety: Ungültige Hardwarewerte aktivieren das Backend nicht; PIT bleibt Basis-Fallback.
 */
#include "kernel/time/apic.h"
#include "arch/x86/include/interrupt.h"
#include "arch/x86/include/cpu_local.h"
#include "arch/x86/include/sys.h"
#include "arch/x86/mm/paging.h"
#include "kernel/sched/scheduler.h"
#include "kernel/time/pit.h"
#include "include/kernel/kernel_log.h"
#include "include/lib/spinlock.h"

#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

volatile uint32_t* apic = (volatile uint32_t*)APIC_BASE_ADDR;
volatile uint32_t apic_interrupt_count = 0;
extern void apic_timer_interrupt(void);

#define APIC_ID_REGISTER 0x020U
#define APIC_ICR_LOW_REGISTER 0x300U
#define APIC_ICR_HIGH_REGISTER 0x310U
#define APIC_ICR_DELIVERY_PENDING (1U << 12U)

static bool apic_enabled;
static spinlock_t apic_icr_lock = SPINLOCK_INIT;
static bool calibrate_apic_timer(uint32_t *ticks_out);

static bool apic_send_ipi_locked(uint8_t destination_apic_id,
                                 uint32_t command, uint32_t spin_limit) {
    for (uint32_t spin = 0U;
         (apic[APIC_ICR_LOW_REGISTER / 4U] &
          APIC_ICR_DELIVERY_PENDING) != 0U; ++spin) {
        if (spin >= spin_limit) return false;
        __asm__ __volatile__("pause");
    }
    apic[APIC_ICR_HIGH_REGISTER / 4U] =
        (uint32_t)destination_apic_id << 24U;
    apic[APIC_ICR_LOW_REGISTER / 4U] = command;
    for (uint32_t spin = 0U;
         (apic[APIC_ICR_LOW_REGISTER / 4U] &
          APIC_ICR_DELIVERY_PENDING) != 0U; ++spin) {
        if (spin >= spin_limit) return false;
        __asm__ __volatile__("pause");
    }
    return true;
}

static bool cpu_has_local_apic(void) {
    uint32_t original_flags;
    uint32_t changed_flags;
    __asm__ __volatile__(
        "pushf\n"
        "pop %0\n"
        : "=r"(original_flags)
        :: "memory"
    );
    uint32_t toggled_flags = original_flags ^ (1U << 21);
    __asm__ __volatile__(
        "push %0\n"
        "popf\n"
        :: "r"(toggled_flags)
        : "memory", "cc"
    );
    __asm__ __volatile__(
        "pushf\n"
        "pop %0\n"
        : "=r"(changed_flags)
        :: "memory"
    );
    __asm__ __volatile__(
        "push %0\n"
        "popf\n"
        :: "r"(original_flags)
        : "memory", "cc"
    );
    if (((changed_flags ^ original_flags) & (1U << 21)) == 0) {
        return false;
    }

    uint32_t eax = 0, ebx, ecx, edx;
    __asm__ __volatile__("cpuid"
                         : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 1U) return false;
    eax = 1;
    __asm__ __volatile__("cpuid"
                         : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx & (1U << 9)) != 0;
}

void apic_timer_isr(void* r) {
    irq_context_note_vector(APIC_VECTOR_BASE);
    irq_context_enter();
    // Acknowledge before switching away from this interrupt stack. Otherwise
    // the LAPIC blocks subsequent timer vectors until this context returns.
    apic[0xB0 / 4] = 0;
    irq_context_exit();
    scheduler_interrupt_handler();
}

void apic_timer_stop() {
    // Mask the timer to stop it
    apic[APIC_LVT_TIMER / 4] |= TIMER_MASKED;
}

uint64_t read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

void write_msr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static bool enable_apic(void) {
    uint64_t apic_base = read_msr(IA32_APIC_BASE_MSR);
    uint64_t physical_base = apic_base & 0xFFFFFFFFFFFFF000ULL;
    if (physical_base > UINT32_MAX) return false;
    uint32_t physical_base32 = (uint32_t)physical_base;

    /* Process page directories are created by copying the kernel mappings.
     * The current ABI executes device IRQs while a user CR3 is active, so a
     * non-standard LAPIC base cannot be installed after user address spaces
     * already exist without propagating that PDE to each of them. */
    if (physical_base32 != APIC_BASE_ADDR) return false;

    void *mapping = map_kernel_mmio(physical_base32, 4096U);
    if (mapping == NULL) return false;
    apic = (volatile uint32_t*)mapping;
    apic_base |= APIC_BASE_ENABLE; // Enable the APIC
    write_msr(IA32_APIC_BASE_MSR, apic_base);
    apic[0xF0 / 4] = APIC_SPURIOUS_ENABLE | APIC_SPURIOUS_VECTOR;
    apic_enabled = true;
    return true;
}

bool apic_is_available(void) {
    return apic_enabled;
}

bool apic_enable_current_cpu_ipi_only(void) {
    if (!apic_enabled || !cpu_has_local_apic()) return false;
    uint64_t apic_base = read_msr(IA32_APIC_BASE_MSR);
    if ((apic_base & 0xFFFFFFFFFFFFF000ULL) != APIC_BASE_ADDR) return false;
    write_msr(IA32_APIC_BASE_MSR, apic_base | APIC_BASE_ENABLE);
    /* APs are coherence workers only at this stage.  Mask every local source
     * and enable solely fixed IPIs plus the architectural spurious vector. */
    apic[APIC_LVT_TIMER / 4U] = TIMER_MASKED;
    apic[0x350U / 4U] = TIMER_MASKED;
    apic[0x360U / 4U] = TIMER_MASKED;
    apic[0x370U / 4U] = TIMER_MASKED;
    apic[0xF0U / 4U] = APIC_SPURIOUS_ENABLE | APIC_SPURIOUS_VECTOR;
    __sync_synchronize();
    return (apic[0xF0U / 4U] & APIC_SPURIOUS_ENABLE) != 0U;
}

bool apic_calibrate_current_cpu_timer_masked(uint32_t *ticks_out) {
    if (!apic_enabled || ticks_out == NULL) return false;
    uint32_t ticks;
    if (!calibrate_apic_timer(&ticks)) return false;
    apic[APIC_LVT_TIMER / 4U] = TIMER_MASKED | APIC_VECTOR_BASE;
    x86_cpu_local_t *local = x86_cpu_local_current();
    if (local == NULL) return false;
    local->lapic_timer_ticks = ticks;
    __sync_synchronize();
    local->lapic_timer_calibrated = 1U;
    *ticks_out = ticks;
    return true;
}

bool apic_start_current_cpu_scheduler_timer(void) {
    x86_cpu_local_t *local = x86_cpu_local_current();
    if (!apic_enabled || local == NULL || local->cpu_index == 0U ||
        local->online == 0U || local->lapic_timer_calibrated == 0U ||
        local->lapic_timer_ticks == 0U) return false;
    init_apic_timer(local->lapic_timer_ticks);
    __sync_synchronize();
    return (apic[APIC_LVT_TIMER / 4U] & TIMER_MASKED) == 0U;
}

uint8_t apic_local_id(void) {
    if (!apic_enabled) return 0U;
    return (uint8_t)(apic[APIC_ID_REGISTER / 4U] >> 24U);
}

bool apic_send_ipi(uint8_t destination_apic_id, uint32_t command,
                   uint64_t deadline_ms) {
    if (!apic_enabled || !irq_enabled() || irq_in_context()) return false;
    if (pit_monotonic_ms() >= deadline_ms) return false;
    uint32_t flags = spinlock_acquire_irq(&apic_icr_lock);
    bool sent = apic_send_ipi_locked(destination_apic_id, command,
                                     1U << 20U);
    spinlock_release_irq(&apic_icr_lock, flags);
    return sent && pit_monotonic_ms() < deadline_ms;
}

bool apic_send_ipi_bounded(uint8_t destination_apic_id, uint32_t command,
                           uint32_t spin_limit) {
    if (!apic_enabled || spin_limit == 0U || irq_in_context()) return false;
    uint32_t flags = spinlock_acquire_irq(&apic_icr_lock);
    bool sent = apic_send_ipi_locked(destination_apic_id, command, spin_limit);
    spinlock_release_irq(&apic_icr_lock, flags);
    return sent;
}

void apic_eoi(void) {
    if (apic_enabled) apic[0xB0U / 4U] = 0U;
}

void init_apic_timer(uint32_t ticks) {
    // Set the divide configuration (divide by 16)
    apic[0x3E0 / 4] = 0x3;

    // Set the timer mode to periodic and assign the interrupt vector
    apic[0x320 / 4] = TIMER_PERIODIC_MODE | APIC_VECTOR_BASE;

    // Writing the initial count starts the timer, so do it last.
    apic[0x380 / 4] = ticks;
}

static bool calibrate_apic_timer(uint32_t *ticks_out) {
    if (ticks_out == NULL) return false;
    const uint64_t sample_ms = 20U;
    const uint32_t loop_limit = 100000000U;

    /* Run the local timer masked in one-shot mode while the already active
     * 1-ms PIT supplies the reference interval. */
    apic[APIC_TIMER_DIVIDE / 4] = 0x3;
    apic[APIC_LVT_TIMER / 4] = TIMER_MASKED | APIC_VECTOR_BASE;
    uint64_t start_ms = pit_monotonic_ms();
    apic[APIC_TIMER_INIT_CNT / 4] = UINT32_MAX;

    uint64_t elapsed_ms = 0;
    for (uint32_t loop = 0; loop < loop_limit; ++loop) {
        elapsed_ms = pit_monotonic_ms() - start_ms;
        if (elapsed_ms >= sample_ms) break;
        __asm__ __volatile__("pause");
    }
    uint32_t elapsed_counts = UINT32_MAX -
                              apic[APIC_TIMER_CURR_CNT / 4];
    if (elapsed_ms == 0 || elapsed_counts == 0) {
        return false;
    }

    uint64_t periodic_counts =
        ((uint64_t)elapsed_counts * APIC_SCHEDULER_PERIOD_MS) / elapsed_ms;
    if (periodic_counts == 0 || periodic_counts > UINT32_MAX) {
        return false;
    }
    *ticks_out = (uint32_t)periodic_counts;
    return true;
}

void initialize_apic_timer(void) {
    apic_enabled = false;
    scheduler_set_apic_timer_active(false);
    if (!cpu_has_local_apic()) {
        klog(KLOG_WARN, "apic",
             "Local APIC unavailable; using PIT scheduler fallback");
        return;
    }

    /* Keep the LAPIC vector outside the remapped PIC range (0x20-0x2f). */
    uint32_t flags = irq_save();
    set_idt_entry(APIC_VECTOR_BASE, (uint32_t)(uintptr_t)apic_timer_interrupt);
    if (!enable_apic()) {
        irq_restore(flags);
        klog(KLOG_WARN, "apic",
             "Unable to map local APIC; using PIT scheduler fallback");
        return;
    }
    irq_restore(flags);

    uint32_t ticks = APIC_DEFAULT_TIMER_TICKS;
    bool calibrated = calibrate_apic_timer(&ticks);
    x86_cpu_local_t *local = x86_cpu_local_current();
    if (local == NULL || ticks == 0U) {
        klog(KLOG_WARN, "apic",
             "Timer calibration has no CPU-local publication slot");
        return;
    }
    local->lapic_timer_ticks = ticks;
    local->lapic_timer_calibrated = calibrated ? 1U : 0U;
    flags = irq_save();
    init_apic_timer(ticks);
    scheduler_set_apic_timer_active(true);
    irq_restore(flags);

    klog(calibrated ? KLOG_INFO : KLOG_WARN, "apic",
         calibrated
             ? "Timer calibrated to %u ms (%u ticks) on vector %u"
             : "Timer calibration failed; using fallback %u ms (%u ticks) on vector %u",
         APIC_SCHEDULER_PERIOD_MS, ticks, APIC_VECTOR_BASE);
}
