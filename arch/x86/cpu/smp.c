/**
 * @file arch/x86/cpu/smp.c
 * @brief Bounded xAPIC application-processor bootstrap.
 *
 * Every AP first proves that it can enter protected paged C code on a private
 * stack and remains in an IPI-only idle loop.  A separate bounded boot barrier
 * later releases one isolated, CPU-affined scheduler probe per AP.
 */
#include "arch/x86/include/smp.h"

#include "arch/x86/include/interrupt.h"
#include "arch/x86/include/cpu_local.h"
#include "arch/x86/include/sys.h"
#include "arch/x86/include/tss.h"
#include "arch/x86/mm/paging.h"
#include "arch/x86/platform/acpi.h"
#include "kernel/time/apic.h"
#include "kernel/time/pit.h"
#include "kernel/sched/mutex.h"
#include "kernel/sched/scheduler.h"
#include "include/lib/spinlock.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"

#include <stddef.h>
#include <stdint.h>

#define X86_SMP_STATUS_VERSION 1U
#define SMP_TRAMPOLINE_VECTOR (X86_SMP_TRAMPOLINE_BASE >> 12U)
#define SMP_TRAMPOLINE_CAPACITY 512U
#define SMP_INIT_ASSERT_DELAY_MS 10U
#define SMP_INIT_DEASSERT_DELAY_MS 10U
#define SMP_SIPI_RETRY_DELAY_MS 1U
#define SMP_IPI_TIMEOUT_MS 20U
#define SMP_AP_START_TIMEOUT_MS 250U
#define SMP_SCHEDULER_TIMEOUT_MS 2000U
#define SMP_PARALLEL_PROBE_TIMEOUT_MS 1000U
#define SMP_RESCHEDULE_IPI_SPIN_LIMIT 4096U

enum {
    SMP_AP_STATE_EMPTY = 0U,
    SMP_AP_STATE_PREPARED = 1U,
    SMP_AP_STATE_ENTERED = 2U,
    SMP_AP_STATE_ONLINE = 3U,
    SMP_AP_STATE_FAILED = 4U,
};

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} x86_descriptor_pointer_t;

typedef struct __attribute__((packed)) {
    volatile uint32_t state;
    uint32_t target_apic_id;
    uint32_t cpu_index;
    uint32_t stack_top;
    uint32_t entry;
    uint32_t cr3;
    x86_descriptor_pointer_t gdtr;
    x86_descriptor_pointer_t idtr;
} smp_ap_mailbox_t;

_Static_assert(sizeof(smp_ap_mailbox_t) == 36U,
               "AP bootstrap mailbox ABI changed");
_Static_assert(SMP_TRAMPOLINE_CAPACITY <=
                   (X86_SMP_TRAMPOLINE_REGION_SIZE / 2U),
               "AP trampoline overlaps its mailbox");

extern const uint8_t x86_ap_trampoline_start[];
extern const uint8_t x86_ap_trampoline_end[];

static uint32_t *ap_idle_stacks[X86_SMP_MAX_CPUS - 1U];
static volatile uint32_t ap_online[X86_SMP_MAX_CPUS];
static spinlock_t smp_probe_lock = SPINLOCK_INIT;
static volatile uint32_t smp_lock_probe_mask;
static volatile uint32_t smp_scheduler_release_mask;
static volatile uint32_t smp_scheduler_ack_mask;
static volatile uint32_t smp_scheduler_entered_mask;
static volatile uint32_t smp_scheduler_settled_mask;
static kernel_mutex_t smp_scheduler_mutex = KERNEL_MUTEX_INIT;
static volatile uint32_t smp_mutex_probe_mask;
static volatile uint32_t smp_mutex_probe_count;
static volatile uint32_t smp_parallel_probe_arrived_mask;
static volatile uint32_t smp_parallel_probe_passed_mask;
static x86_smp_parallel_probe_t smp_parallel_probe;
static x86_smp_status_t smp_status;

static void smp_scheduler_probe_task(void) {
    uint32_t cpu_index = x86_cpu_current_index();
    KASSERT(cpu_index > 0U && cpu_index < X86_SMP_MAX_CPUS);
    KASSERT((smp_scheduler_release_mask & (1U << cpu_index)) != 0U);
    KASSERT(kernel_mutex_lock_for(&smp_scheduler_mutex, 500U) == 0);
    uint32_t before = smp_mutex_probe_count;
    pit_delay(1U);
    smp_mutex_probe_count = before + 1U;
    smp_mutex_probe_mask |= 1U << cpu_index;
    kernel_mutex_unlock(&smp_scheduler_mutex);

    uint32_t cpu_bit = 1U << cpu_index;
    __sync_fetch_and_or(&smp_parallel_probe_arrived_mask, cpu_bit);
    __sync_synchronize();
    uint64_t deadline = pit_monotonic_ms() + SMP_PARALLEL_PROBE_TIMEOUT_MS;
    while ((smp_parallel_probe_arrived_mask & smp_scheduler_release_mask) !=
           smp_scheduler_release_mask) {
        if (pit_monotonic_ms() >= deadline) break;
        pit_delay(1U);
    }
    if ((smp_parallel_probe_arrived_mask & smp_scheduler_release_mask) ==
            smp_scheduler_release_mask &&
        smp_parallel_probe != NULL && smp_parallel_probe(cpu_index))
        __sync_fetch_and_or(&smp_parallel_probe_passed_mask, cpu_bit);
    __sync_fetch_and_or(&smp_scheduler_entered_mask, 1U << cpu_index);
    __sync_synchronize();
}

bool x86_smp_set_parallel_probe(x86_smp_parallel_probe_t probe) {
    if (probe == NULL || smp_scheduler_release_mask != 0U ||
        smp_parallel_probe != NULL) return false;
    smp_parallel_probe = probe;
    __sync_synchronize();
    return true;
}

void x86_smp_scheduler_release_isr(void *frame) {
    (void)frame;
    irq_context_note_vector(X86_SMP_SCHEDULER_RELEASE_VECTOR);
    irq_context_enter();
    uint32_t cpu_index = x86_cpu_current_index();
    uint32_t cpu_bit = cpu_index < X86_SMP_MAX_CPUS
        ? 1U << cpu_index : 0U;
    if (cpu_index == 0U || cpu_bit == 0U ||
        (smp_scheduler_release_mask & cpu_bit) == 0U ||
        (smp_scheduler_ack_mask & cpu_bit) != 0U ||
        !apic_start_current_cpu_scheduler_timer()) {
        apic_eoi();
        irq_context_exit();
        return;
    }
    __sync_fetch_and_or(&smp_scheduler_ack_mask, cpu_bit);
    __sync_synchronize();
    apic_eoi();
    irq_context_exit();
}

bool x86_smp_request_reschedule(uint32_t cpu_mask) {
    uint32_t online_count = smp_status.online_cpu_count;
    if (cpu_mask == 0U || online_count == 0U) return true;
    if (online_count > X86_SMP_MAX_CPUS || irq_in_context()) return false;

    uint32_t current_cpu = x86_cpu_current_index();
    if (current_cpu >= online_count) return false;
    uint32_t online_mask = (1U << online_count) - 1U;
    uint32_t eligible_mask = smp_scheduler_ack_mask | 1U;
    uint32_t target_mask = cpu_mask & online_mask & eligible_mask &
        ~(1U << current_cpu);
    bool sent_all = true;
    for (uint32_t cpu = 0U; cpu < online_count; ++cpu) {
        uint32_t cpu_bit = 1U << cpu;
        if ((target_mask & cpu_bit) == 0U) continue;
        x86_cpu_local_t *local = x86_cpu_local_by_index(cpu);
        if (local == NULL || local->online == 0U ||
            local->apic_id != smp_status.apic_ids[cpu] ||
            !apic_send_ipi_bounded(
                (uint8_t)smp_status.apic_ids[cpu],
                APIC_IPI_FIXED(X86_SMP_RESCHEDULE_VECTOR),
                SMP_RESCHEDULE_IPI_SPIN_LIMIT))
            sent_all = false;
    }
    return sent_all;
}

void x86_smp_reschedule_isr(void *frame) {
    (void)frame;
    irq_context_note_vector(X86_SMP_RESCHEDULE_VECTOR);
    irq_context_enter();
    /* Acknowledge before a possible context switch so the local APIC can
     * accept subsequent wakeups even while this interrupt frame is parked. */
    apic_eoi();
    irq_context_exit();
    scheduler_interrupt_handler();
}

static void smp_lock_probe_publish(uint32_t cpu_index) {
    KASSERT(cpu_index < X86_SMP_MAX_CPUS);
    uint32_t flags = spinlock_acquire_irq(&smp_probe_lock);
    KASSERT((smp_lock_probe_mask & (1U << cpu_index)) == 0U);
    smp_lock_probe_mask |= 1U << cpu_index;
    spinlock_release_irq(&smp_probe_lock, flags);
}

static smp_ap_mailbox_t *mailbox(void) {
    return (smp_ap_mailbox_t *)(uintptr_t)(X86_SMP_TRAMPOLINE_BASE +
                                           SMP_TRAMPOLINE_CAPACITY);
}

static uint32_t current_cr3(void) {
    uint32_t value;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(value));
    return value;
}

static void descriptor_tables(x86_descriptor_pointer_t *gdtr,
                              x86_descriptor_pointer_t *idtr) {
    __asm__ __volatile__("sgdt %0" : "=m"(*gdtr));
    __asm__ __volatile__("sidt %0" : "=m"(*idtr));
}

static bool wait_for_ap_state(uint64_t deadline_ms, uint32_t expected) {
    while (mailbox()->state != expected) {
        if (pit_monotonic_ms() >= deadline_ms) return false;
        cpu_halt();
    }
    return true;
}

static bool send_ipi(uint8_t apic_id, uint32_t command) {
    return apic_send_ipi(apic_id, command,
                         pit_monotonic_ms() + SMP_IPI_TIMEOUT_MS);
}

static bool start_ap(uint32_t cpu_index, uint8_t apic_id) {
    smp_ap_mailbox_t *shared = mailbox();
    x86_descriptor_pointer_t gdtr;
    x86_descriptor_pointer_t idtr;
    descriptor_tables(&gdtr, &idtr);

    if (!x86_cpu_local_register(cpu_index, apic_id)) return false;
    uint32_t *idle_stack = scheduler_allocate_kernel_stack();
    if (idle_stack == NULL) return false;
    ap_idle_stacks[cpu_index - 1U] = idle_stack;
    x86_cpu_local_t *local = x86_cpu_local_by_index(cpu_index);
    if (local == NULL) return false;
    local->kernel_idle_stack_low = (uint32_t)(uintptr_t)idle_stack;
    local->kernel_idle_stack_high =
        (uint32_t)(uintptr_t)idle_stack + STACK_SIZE;

    memset(shared, 0, sizeof(*shared));
    shared->target_apic_id = apic_id;
    shared->cpu_index = cpu_index;
    shared->stack_top = local->kernel_idle_stack_high;
    shared->entry = (uint32_t)(uintptr_t)x86_smp_ap_entry;
    shared->cr3 = current_cr3();
    shared->gdtr = gdtr;
    shared->idtr = idtr;
    __sync_synchronize();
    shared->state = SMP_AP_STATE_PREPARED;

    if (!send_ipi(apic_id, APIC_IPI_INIT_ASSERT)) return false;
    pit_delay(SMP_INIT_ASSERT_DELAY_MS);
    if (!send_ipi(apic_id, APIC_IPI_INIT_DEASSERT)) return false;
    pit_delay(SMP_INIT_DEASSERT_DELAY_MS);

    uint32_t sipi = APIC_IPI_STARTUP | SMP_TRAMPOLINE_VECTOR;
    if (!send_ipi(apic_id, sipi)) return false;
    pit_delay(SMP_SIPI_RETRY_DELAY_MS);
    if (shared->state != SMP_AP_STATE_ONLINE && !send_ipi(apic_id, sipi))
        return false;

    return wait_for_ap_state(pit_monotonic_ms() + SMP_AP_START_TIMEOUT_MS,
                             SMP_AP_STATE_ONLINE);
}

__attribute__((noreturn)) void x86_smp_ap_entry(uint32_t cpu_index) {
    smp_ap_mailbox_t *shared = mailbox();
    shared->state = SMP_AP_STATE_ENTERED;
    __sync_synchronize();
    x86_cpu_local_t *local = x86_cpu_local_current();
    uint32_t timer_ticks = 0U;
    if (cpu_index == 0U || cpu_index >= X86_SMP_MAX_CPUS || local == NULL ||
        local->cpu_index != cpu_index ||
        apic_local_id() != (uint8_t)shared->target_apic_id ||
        !apic_enable_current_cpu_ipi_only() ||
        !apic_calibrate_current_cpu_timer_masked(&timer_ticks) ||
        timer_ticks == 0U ||
        !tss_init_cpu(cpu_index, shared->stack_top, 0x10U) ||
        !gdt_install_cpu(cpu_index)) {
        shared->state = SMP_AP_STATE_FAILED;
        cpu_halt_forever();
    }
    switch_page_directory(paging_kernel_directory());
    if (paging_current_directory() != paging_kernel_directory() ||
        !x86_cpu_local_mark_online(cpu_index)) {
        shared->state = SMP_AP_STATE_FAILED;
        cpu_halt_forever();
    }
    smp_lock_probe_publish(cpu_index);
    ap_online[cpu_index] = 1U;
    __sync_synchronize();
    shared->state = SMP_AP_STATE_ONLINE;
    irq_enable();
    while (1) {
        cpu_halt();
        uint32_t cpu_bit = 1U << cpu_index;
        if ((smp_scheduler_entered_mask & cpu_bit) != 0U &&
            local->scheduler_current_task == -1 &&
            local->scheduler_handoff_task == -1 &&
            local->scheduler_context_saved != 0U) {
            __sync_fetch_and_or(&smp_scheduler_settled_mask, cpu_bit);
        }
    }
}

bool x86_smp_initialize(void) {
    memset(&smp_status, 0, sizeof(smp_status));
    memset((void *)ap_online, 0, sizeof(ap_online));
    memset(ap_idle_stacks, 0, sizeof(ap_idle_stacks));
    spinlock_init(&smp_probe_lock);
    smp_lock_probe_mask = 0U;
    smp_scheduler_release_mask = 0U;
    smp_scheduler_ack_mask = 0U;
    smp_scheduler_entered_mask = 0U;
    smp_scheduler_settled_mask = 0U;
    kernel_mutex_init(&smp_scheduler_mutex);
    smp_mutex_probe_mask = 0U;
    smp_mutex_probe_count = 0U;
    smp_parallel_probe_arrived_mask = 0U;
    smp_parallel_probe_passed_mask = 0U;
    smp_parallel_probe = NULL;
    smp_lock_probe_publish(0U);
    smp_status.version = X86_SMP_STATUS_VERSION;
    smp_status.online_cpu_count = 1U;

    if (!apic_is_available()) {
        printf("REIST_SMP DEGRADED reason=lapic-unavailable online=1\n");
        return false;
    }
    smp_status.bsp_apic_id = apic_local_id();
    x86_cpu_local_t *bsp_local = x86_cpu_local_current();
    if (bsp_local == NULL || bsp_local->cpu_index != 0U ||
        bsp_local->apic_id != smp_status.bsp_apic_id) {
        printf("REIST_SMP DEGRADED reason=bsp-identity online=1\n");
        return false;
    }
    smp_status.apic_ids[0] = smp_status.bsp_apic_id;

    x86_acpi_cpu_inventory_t inventory;
    if (!x86_acpi_cpu_inventory(&inventory) ||
        inventory.local_apic_address != APIC_BASE_ADDR) {
        printf("REIST_SMP DEGRADED reason=madt-unavailable online=1\n");
        return false;
    }
    smp_status.discovered_cpu_count = inventory.discovered_cpu_count;
    smp_status.usable_cpu_count = inventory.usable_cpu_count;
    printf("REIST_SMP DISCOVERED cpus=%u bsp=%u usable=%u\n",
           inventory.discovered_cpu_count, smp_status.bsp_apic_id,
           inventory.usable_cpu_count);

    size_t trampoline_size =
        (size_t)(x86_ap_trampoline_end - x86_ap_trampoline_start);
    if (trampoline_size == 0U || trampoline_size > SMP_TRAMPOLINE_CAPACITY) {
        printf("REIST_SMP DEGRADED reason=trampoline-size size=%u\n",
               (unsigned)trampoline_size);
        return false;
    }
    memset((void *)(uintptr_t)X86_SMP_TRAMPOLINE_BASE, 0,
           X86_SMP_TRAMPOLINE_REGION_SIZE);
    memcpy((void *)(uintptr_t)X86_SMP_TRAMPOLINE_BASE,
           x86_ap_trampoline_start, trampoline_size);
    __sync_synchronize();

    uint32_t next_index = 1U;
    for (uint32_t entry = 0U;
         entry < inventory.usable_cpu_count &&
         next_index < X86_SMP_MAX_CPUS; ++entry) {
        uint8_t apic_id = (uint8_t)inventory.apic_ids[entry];
        if (apic_id == smp_status.bsp_apic_id) continue;
        smp_status.apic_ids[next_index] = apic_id;
        if (!start_ap(next_index, apic_id)) {
            mailbox()->state = SMP_AP_STATE_FAILED;
            ++smp_status.failed_ap_count;
            printf("REIST_SMP AP_FAILED index=%u apic=%u\n",
                   next_index, apic_id);
            /* Never reuse a mailbox after a timeout: a late AP could consume
             * a newer CPU's stack and identity. */
            break;
        }
        ++smp_status.online_cpu_count;
        ++smp_status.parked_ap_count;
        printf("REIST_SMP AP_ONLINE index=%u apic=%u\n",
               next_index, apic_id);
        ++next_index;
    }
    printf("REIST_SMP READY online=%u parked=%u failed=%u\n",
           smp_status.online_cpu_count, smp_status.parked_ap_count,
           smp_status.failed_ap_count);
    uint32_t per_cpu_ready = 0U;
    for (uint32_t index = 0U; index < smp_status.online_cpu_count; ++index) {
        x86_cpu_local_t *local = x86_cpu_local_by_index(index);
        if (local != NULL && local->online != 0U &&
            local->cpu_index == index) ++per_cpu_ready;
    }
    printf("REIST_SMP PERCPU_READY cpus=%u\n", per_cpu_ready);
    uint32_t expected_probe_mask =
        (1U << smp_status.online_cpu_count) - 1U;
    if (smp_lock_probe_mask != expected_probe_mask) {
        printf("REIST_SMP LOCK_FAILED mask=%08X expected=%08X\n",
               smp_lock_probe_mask, expected_probe_mask);
        return false;
    }
    printf("REIST_SMP LOCK_READY cpus=%u mask=%08X\n",
           smp_status.online_cpu_count, smp_lock_probe_mask);
    if (!paging_tlb_shootdown_probe()) {
        printf("REIST_SMP TLB_FAILED\n");
        return false;
    }
    printf("REIST_SMP TLB_READY cpus=%u\n",
           smp_status.online_cpu_count);
    if (!irq_affinity_bsp_only_ready()) {
        printf("REIST_SMP IRQ_AFFINITY_FAILED\n");
        return false;
    }
    printf("REIST_SMP IRQ_AFFINITY_READY mode=pic-bsp\n");
    uint32_t calibrated_cpus = 0U;
    for (uint32_t cpu = 0U; cpu < smp_status.online_cpu_count; ++cpu) {
        x86_cpu_local_t *local = x86_cpu_local_by_index(cpu);
        if (local != NULL && local->lapic_timer_calibrated != 0U &&
            local->lapic_timer_ticks != 0U) ++calibrated_cpus;
    }
    if (calibrated_cpus != smp_status.online_cpu_count) {
        printf("REIST_SMP TIMER_FAILED calibrated=%u expected=%u\n",
               calibrated_cpus, smp_status.online_cpu_count);
        return false;
    }
    printf("REIST_SMP TIMER_READY cpus=%u mode=masked\n", calibrated_cpus);
    return smp_status.failed_ap_count == 0U;
}

bool x86_smp_scheduler_probe(void) {
    if (smp_status.online_cpu_count <= 1U) {
        printf("REIST_SMP MUTEX_READY workers=0 mask=00000000\n");
        printf("REIST_SMP INTEGRITY_READY workers=0 mask=00000000\n");
        printf("REIST_SMP SUBSYSTEM_READY workers=0 mask=00000000\n");
        printf("REIST_SMP REAP_READY workers=0 reaped=0\n");
        printf("REIST_SMP SCHEDULER_READY cpus=1 probe_mask=00000000\n");
        return true;
    }
    if (smp_status.online_cpu_count > X86_SMP_MAX_CPUS ||
        smp_status.failed_ap_count != 0U || smp_scheduler_release_mask != 0U)
        return false;

    uint32_t expected_mask =
        ((1U << smp_status.online_cpu_count) - 1U) & ~1U;
    for (uint32_t cpu = 1U; cpu < smp_status.online_cpu_count; ++cpu) {
        uint32_t *stack = scheduler_allocate_kernel_stack();
        if (stack == NULL ||
            create_affined_kernel_task(smp_scheduler_probe_task, stack,
                                       1U << cpu) < 0) {
            if (stack != NULL) scheduler_free_kernel_stack(stack);
            return false;
        }
    }

    __sync_synchronize();
    smp_scheduler_release_mask = expected_mask;
    __sync_synchronize();
    for (uint32_t cpu = 1U; cpu < smp_status.online_cpu_count; ++cpu) {
        if (!apic_send_ipi((uint8_t)smp_status.apic_ids[cpu],
                           APIC_IPI_FIXED(X86_SMP_SCHEDULER_RELEASE_VECTOR),
                           pit_monotonic_ms() + SMP_IPI_TIMEOUT_MS))
            return false;
    }

    uint64_t deadline = pit_monotonic_ms() + SMP_SCHEDULER_TIMEOUT_MS;
    while ((smp_scheduler_ack_mask & expected_mask) != expected_mask ||
           (smp_scheduler_settled_mask & expected_mask) != expected_mask) {
        if (pit_monotonic_ms() >= deadline) {
            printf("REIST_SMP SCHEDULER_FAILED ack=%08X entered=%08X settled=%08X expected=%08X\n",
                   smp_scheduler_ack_mask, smp_scheduler_entered_mask,
                   smp_scheduler_settled_mask, expected_mask);
            return false;
        }
        cpu_halt();
    }
    if (smp_mutex_probe_mask != expected_mask ||
        smp_mutex_probe_count != smp_status.online_cpu_count - 1U) {
        printf("REIST_SMP MUTEX_FAILED count=%u mask=%08X expected=%08X\n",
               smp_mutex_probe_count, smp_mutex_probe_mask, expected_mask);
        return false;
    }
    if (smp_parallel_probe == NULL ||
        smp_parallel_probe_arrived_mask != expected_mask ||
        smp_parallel_probe_passed_mask != expected_mask) {
        printf("REIST_SMP SUBSYSTEM_FAILED arrived=%08X passed=%08X expected=%08X\n",
               smp_parallel_probe_arrived_mask,
               smp_parallel_probe_passed_mask, expected_mask);
        return false;
    }
    size_t reaped = scheduler_reap_finished_tasks();
    if (reaped < smp_status.online_cpu_count - 1U) {
        printf("REIST_SMP REAP_FAILED reaped=%u expected=%u\n",
               (uint32_t)reaped, smp_status.online_cpu_count - 1U);
        return false;
    }
    printf("REIST_SMP MUTEX_READY workers=%u mask=%08X\n",
           smp_mutex_probe_count, smp_mutex_probe_mask);
    printf("REIST_SMP INTEGRITY_READY workers=%u mask=%08X\n",
           smp_status.online_cpu_count - 1U,
           smp_parallel_probe_passed_mask);
    printf("REIST_SMP SUBSYSTEM_READY workers=%u mask=%08X\n",
           smp_status.online_cpu_count - 1U,
           smp_parallel_probe_passed_mask);
    printf("REIST_SMP REAP_READY workers=%u reaped=%u\n",
           smp_status.online_cpu_count - 1U, (uint32_t)reaped);
    printf("REIST_SMP SCHEDULER_READY cpus=%u probe_mask=%08X\n",
           smp_status.online_cpu_count, smp_scheduler_settled_mask);
    return true;
}

void x86_smp_status(x86_smp_status_t *status) {
    if (status != NULL) *status = smp_status;
}
