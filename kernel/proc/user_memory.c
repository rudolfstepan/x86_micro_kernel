/* One execution context owns each process heap. Pages, including unfinished
 * mappings, belong to its directory before any scheduling handoff. A killed
 * syscall never resumes; ordinary failure rolls back before returning. */
#include "kernel/proc/process.h"
#include "kernel/sched/scheduler.h"
#include "arch/x86/mm/paging.h"
#include "arch/x86/include/interrupt.h"
#include "mm/kmalloc.h"
#include "lib/libc/string.h"
#include "include/kernel/panic.h"

static void validate_user_heap(const Process *process) {
    KASSERT(process->heap_budget <= PROCESS_HEAP_MAX_BYTES &&
            process->heap_budget % PAGE_SIZE == 0U);
    uint32_t used = 0U;
    for (unsigned i = 0U; i < MAX_USER_ALLOCATIONS; ++i) {
        const user_allocation_t *a = &process->user_allocations[i];
        if (!a->allocated) {
            KASSERT(!a->address && !a->mapped_size && !a->requested_size);
            continue;
        }
        KASSERT(a->address >= USER_HEAP_BASE && a->address < USER_HEAP_TOP &&
                a->address % PAGE_SIZE == 0U && a->mapped_size &&
                a->mapped_size % PAGE_SIZE == 0U &&
                a->mapped_size <= USER_HEAP_TOP-a->address &&
                a->requested_size && a->requested_size <= a->mapped_size &&
                a->mapped_size <= PROCESS_HEAP_MAX_BYTES-used);
        used += a->mapped_size;
        for (unsigned j = 0U; j < i; ++j) {
            const user_allocation_t *b = &process->user_allocations[j];
            KASSERT(!b->allocated || a->address >= b->address+b->mapped_size ||
                    b->address >= a->address+a->mapped_size);
        }
    }
    KASSERT(used == process->heap_bytes && used <= process->heap_budget);
}

static void memory_handoff(uint32_t pages) {
    if (pages % PROCESS_MEMORY_BATCH_PAGES == 0U && scheduler_can_sleep())
        (void)scheduler_yield();
}

static void release_user_pages(page_directory_t *pd, uint32_t address,
                               uint32_t length) {
    for (uint32_t offset = 0U; offset < length; offset += PAGE_SIZE) {
        /* Revocation and physical release are atomic inside unmap_page. */
        KASSERT(unmap_page(pd, address + offset, true) == 0);
        memory_handoff(offset / PAGE_SIZE + 1U);
    }
    paging_trim_user_tables(pd, address, length);
}

static int map_user_allocation(page_directory_t *pd,
                                const user_allocation_t *allocation) {
    uint32_t mapped = 0U;
    while (mapped < allocation->mapped_size) {
        /* Do not leave an unowned frame on the kernel stack across a kill.
         * Interrupts stay enabled during zeroing; PTE/PMM locks are per-page. */
        scheduler_preempt_disable();
        uint32_t frame = (uint32_t)allocate_user_frame();
        int result = -1;
        if (frame != 0U) {
            memset((void *)(uintptr_t)frame, 0, PAGE_SIZE);
            result = map_page(pd, allocation->address + mapped, frame,
                              PAGE_USER | PAGE_RW);
            if (result != 0) free_frame(frame);
        }
        scheduler_preempt_enable();
        if (result != 0) {
            release_user_pages(pd, allocation->address, mapped);
            return -1;
        }
        mapped += PAGE_SIZE;
        memory_handoff(mapped / PAGE_SIZE);
    }
    return 0;
}

void *process_user_malloc(size_t size) {
    Process *process = scheduler_current_process();
    if (!process || !scheduler_can_sleep() || !size ||
        size > PROCESS_HEAP_MAX_BYTES) return NULL;
    validate_user_heap(process);
    uint32_t length = ((uint32_t)size + PAGE_SIZE - 1U) & ~(PAGE_SIZE - 1U);
    if (!process->heap_budget) {
        memory_stats_t stats;
        memory_get_stats(&stats);
        uint64_t budget = stats.managed_bytes / 2U;
        if (budget > PROCESS_HEAP_MAX_BYTES) budget = PROCESS_HEAP_MAX_BYTES;
        process->heap_budget = (uint32_t)budget & ~(PAGE_SIZE - 1U);
    }
    if (process->domain_profile.kind == PROCESS_DOMAIN_SCRIPT &&
        process->heap_budget > PROCESS_SCRIPT_HEAP_MAX_BYTES)
        process->heap_budget = PROCESS_SCRIPT_HEAP_MAX_BYTES;
    if (process->heap_bytes > process->heap_budget ||
        length > process->heap_budget - process->heap_bytes) return NULL;
    user_allocation_t *slot = NULL;
    for (unsigned i = 0U; i < MAX_USER_ALLOCATIONS; ++i)
        if (!process->user_allocations[i].allocated) {
            slot = &process->user_allocations[i]; break;
        }
    if (!slot) return NULL;

    /* First-fit among live intervals; no historical high-water address leak.
     * At most one advancement per live interval, all ranges checked first. */
    uint32_t address = USER_HEAP_BASE;
    bool found = false;
    for (unsigned pass = 0U; pass <= MAX_USER_ALLOCATIONS; ++pass) {
        if (address >= USER_HEAP_TOP || length > USER_HEAP_TOP-address) return NULL;
        bool overlap = false;
        for (unsigned i = 0U; i < MAX_USER_ALLOCATIONS; ++i) {
            const user_allocation_t *a = &process->user_allocations[i];
            if (!a->allocated) continue;
            if (a->address < USER_HEAP_BASE || a->address >= USER_HEAP_TOP ||
                !a->mapped_size || a->mapped_size > USER_HEAP_TOP-a->address)
                return NULL;
            uint32_t end = a->address+a->mapped_size;
            if (address < end && a->address < address+length) {
                address = end; overlap = true; break;
            }
        }
        if (!overlap) { found = true; break; }
    }
    if (!found) return NULL;
    *slot = (user_allocation_t){address, (uint32_t)size, length, true};
    process->heap_bytes += length;
    if (map_user_allocation(paging_current_directory(), slot) != 0) {
        process->heap_bytes -= length;
        memset(slot, 0, sizeof(*slot));
        return NULL;
    }
    return (void *)(uintptr_t)address;
}

int process_user_free(void *pointer) {
    if (!pointer) return 0;
    Process *process = scheduler_current_process();
    if (!process || !scheduler_can_sleep()) return -1;
    validate_user_heap(process);
    for (unsigned i = 0U; i < MAX_USER_ALLOCATIONS; ++i) {
        user_allocation_t *a = &process->user_allocations[i];
        if (a->allocated && (uintptr_t)pointer == a->address) {
            release_user_pages(paging_current_directory(), a->address, a->mapped_size);
            process->heap_bytes -= a->mapped_size;
            memset(a, 0, sizeof(*a));
            return 0;
        }
    }
    return -1;
}

void *process_user_realloc(void *pointer, size_t size) {
    if (!pointer) return process_user_malloc(size);
    if (!size) { (void)process_user_free(pointer); return NULL; }
    Process *process = scheduler_current_process();
    if (!process || !scheduler_can_sleep() || size > PROCESS_HEAP_MAX_BYTES) return NULL;
    validate_user_heap(process);
    user_allocation_t *old = NULL;
    for (unsigned i = 0U; i < MAX_USER_ALLOCATIONS; ++i)
        if (process->user_allocations[i].allocated &&
            (uintptr_t)pointer == process->user_allocations[i].address) {
            old = &process->user_allocations[i]; break;
        }
    if (!old) return NULL;
    uint32_t old_size = old->requested_size;
    if (size <= old->mapped_size) {
        old->requested_size = (uint32_t)size;
        return pointer;
    }
    void *replacement = process_user_malloc(size);
    if (!replacement) return NULL;
    /* Both ranges are owned by the directory throughout this preemptible copy. */
    for (uint32_t offset = 0U; offset < old_size;) {
        uint32_t count = old_size-offset;
        if (count > PROCESS_MEMORY_BATCH_PAGES*PAGE_SIZE)
            count = PROCESS_MEMORY_BATCH_PAGES*PAGE_SIZE;
        memcpy((uint8_t *)replacement+offset, (const uint8_t *)pointer+offset, count);
        offset += count;
        if (scheduler_can_sleep()) (void)scheduler_yield();
    }
    (void)process_user_free(pointer);
    return replacement;
}
