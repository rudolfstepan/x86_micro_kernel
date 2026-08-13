#include "mm/kmalloc.h"
#include "arch/x86/mm/paging.h"
#include "include/lib/spinlock.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"
#include "include/kernel/panic.h"

#include <stdbool.h>
#include <stdint.h>

extern char _stack_end; /* Defined by config/klink.ld. */

#define ALIGN_UP(value, alignment) \
    (((value) + ((alignment) - 1U)) & ~((alignment) - 1U))
#define ALIGN_DOWN(value, alignment) ((value) & ~((alignment) - 1U))

#define FRAME_SIZE             4096U
#define PHYSICAL_MEMORY_LIMIT  ((uint64_t)KERNEL_IDENTITY_LIMIT)
#define MAX_MEMORY_REGIONS     64U
#define BLOCK_MAGIC            0x4B484541U /* "KHEA" */
#define MIN_ALLOCATION         16U
#define INITIAL_HEAP_SIZE      (1U * 1024U * 1024U)
#define HEAP_GROW_CHUNK        (256U * 1024U)

typedef struct {
    uint64_t base;
    uint64_t length;
} physical_region_t;

typedef struct memory_block {
    size_t size;
    struct memory_block *next;
    uint32_t magic;
    uint32_t free;
} memory_block;

_Static_assert(sizeof(memory_block) == 16,
               "heap metadata must preserve 16-byte payload alignment");
_Static_assert(sizeof(memory_stats_t) == 88U,
               "memory statistics ABI size changed");
_Static_assert(offsetof(memory_stats_t, detected_usable_bytes) == 8U,
               "memory statistics ABI header changed");

uint64_t total_memory = 0;

static physical_region_t usable_regions[MAX_MEMORY_REGIONS];
static physical_region_t reserved_regions[MAX_MEMORY_REGIONS];
static size_t usable_region_count;
static size_t reserved_region_count;

static uint8_t *frame_bitmap;
static uint8_t *frame_reserved_bitmap;
static size_t frame_count;
static size_t frame_bitmap_size;
static size_t managed_frame_count;
static size_t reserved_frame_count;
static size_t allocated_frame_count;
static size_t free_frame_count;
static size_t frame_search_hint = 1U;

static uintptr_t heap_begin;
static uintptr_t heap_limit;
static memory_block *free_list;
static size_t heap_arena_count;
static size_t heap_backing_bytes;
static bool memory_initialized;

static spinlock_t heap_lock = SPINLOCK_INIT;
static spinlock_t frame_lock = SPINLOCK_INIT;

static uint64_t saturated_region_end(uint64_t base, uint64_t length) {
    if (length > UINT64_MAX - base) {
        return UINT64_MAX;
    }
    return base + length;
}

void memory_map_reset(void) {
    total_memory = 0;
    usable_region_count = 0;
    reserved_region_count = 0;
    frame_bitmap = NULL;
    frame_reserved_bitmap = NULL;
    frame_count = 0;
    frame_bitmap_size = 0;
    managed_frame_count = 0;
    reserved_frame_count = 0;
    allocated_frame_count = 0;
    free_frame_count = 0;
    frame_search_hint = 1U;
    free_list = NULL;
    heap_begin = 0;
    heap_limit = 0;
    heap_arena_count = 0;
    heap_backing_bytes = 0;
    memory_initialized = false;
}

static int add_normalized_region(physical_region_t *regions, size_t *count,
                                 uint64_t base, uint64_t length) {
    if (regions == NULL || count == NULL || length == 0) return -1;
    uint64_t end = saturated_region_end(base, length);
    if (end <= base) return -1;

    for (size_t i = 0; i < *count;) {
        uint64_t current_end = saturated_region_end(regions[i].base,
                                                     regions[i].length);
        if (base <= current_end && regions[i].base <= end) {
            if (regions[i].base < base) base = regions[i].base;
            if (current_end > end) end = current_end;
            for (size_t move = i + 1U; move < *count; ++move) {
                regions[move - 1U] = regions[move];
            }
            --(*count);
            continue;
        }
        ++i;
    }

    if (*count >= MAX_MEMORY_REGIONS) return -1;
    size_t insert = 0;
    while (insert < *count && regions[insert].base < base) ++insert;
    for (size_t move = *count; move > insert; --move) {
        regions[move] = regions[move - 1U];
    }
    regions[insert].base = base;
    regions[insert].length = end - base;
    ++(*count);
    return 0;
}

static void recompute_total_memory(void) {
    total_memory = 0;
    for (size_t i = 0; i < usable_region_count; ++i) {
        uint64_t length = usable_regions[i].length;
        if (UINT64_MAX - total_memory < length) {
            total_memory = UINT64_MAX;
            return;
        }
        total_memory += length;
    }
}

int memory_add_usable_region(uint64_t base, uint64_t length) {
    if (add_normalized_region(usable_regions, &usable_region_count,
                              base, length) != 0) return -1;
    recompute_total_memory();
    return 0;
}

int memory_reserve_region(uint64_t base, uint64_t length) {
    return add_normalized_region(reserved_regions, &reserved_region_count,
                                 base, length);
}

int memory_region_is_usable(uint64_t base, uint64_t length) {
    if (length == 0 || length > UINT64_MAX - base) {
        return 0;
    }
    uint64_t requested_end = base + length;
    for (size_t i = 0; i < reserved_region_count; ++i) {
        uint64_t reserved_end = saturated_region_end(reserved_regions[i].base,
                                                      reserved_regions[i].length);
        if (base < reserved_end && reserved_regions[i].base < requested_end) {
            return 0;
        }
    }
    for (size_t i = 0; i < usable_region_count; ++i) {
        uint64_t region_end = saturated_region_end(usable_regions[i].base,
                                                   usable_regions[i].length);
        if (base >= usable_regions[i].base && requested_end <= region_end) {
            return 1;
        }
    }
    return 0;
}

static bool frame_index_valid(size_t frame) {
    return frame_bitmap != NULL && frame < frame_count;
}

static void set_bitmap_bit(uint8_t *bitmap, size_t frame) {
    if (bitmap != NULL && frame < frame_count) {
        bitmap[frame / 8U] |= (uint8_t)(1U << (frame % 8U));
    }
}

static void clear_bitmap_bit(uint8_t *bitmap, size_t frame) {
    if (bitmap != NULL && frame < frame_count) {
        bitmap[frame / 8U] &= (uint8_t)~(1U << (frame % 8U));
    }
}

static bool test_bitmap_bit(const uint8_t *bitmap, size_t frame) {
    return bitmap == NULL || frame >= frame_count ||
           (bitmap[frame / 8U] & (uint8_t)(1U << (frame % 8U))) != 0;
}

static void set_frame(size_t frame) {
    set_bitmap_bit(frame_bitmap, frame);
}

static void clear_frame(size_t frame) {
    clear_bitmap_bit(frame_bitmap, frame);
}

static bool test_frame(size_t frame) {
    return test_bitmap_bit(frame_bitmap, frame);
}

static void set_reserved_frame(size_t frame) {
    set_bitmap_bit(frame_reserved_bitmap, frame);
}

static bool test_reserved_frame(size_t frame) {
    return test_bitmap_bit(frame_reserved_bitmap, frame);
}

static bool address_is_usable(uint64_t address);

static void reserve_frame_range(uint64_t base, uint64_t length) {
    uint64_t end = saturated_region_end(base, length);
    if (base >= PHYSICAL_MEMORY_LIMIT || length == 0) {
        return;
    }
    if (end > PHYSICAL_MEMORY_LIMIT) {
        end = PHYSICAL_MEMORY_LIMIT;
    }

    size_t first = (size_t)(base / FRAME_SIZE);
    size_t last = (size_t)((end + FRAME_SIZE - 1U) / FRAME_SIZE);
    if (last > frame_count) {
        last = frame_count;
    }
    for (size_t frame = first; frame < last; ++frame) {
        uint64_t address = (uint64_t)frame * FRAME_SIZE;
        if (!address_is_usable(address) || test_reserved_frame(frame)) {
            continue;
        }
        if (!test_frame(frame)) {
            set_frame(frame);
            if (free_frame_count != 0) --free_frame_count;
        } else if (allocated_frame_count != 0) {
            --allocated_frame_count;
        }
        set_reserved_frame(frame);
        ++reserved_frame_count;
    }
}

static bool address_is_usable(uint64_t address) {
    if (address > UINT64_MAX - FRAME_SIZE) {
        return false;
    }
    uint64_t frame_end = address + FRAME_SIZE;
    for (size_t i = 0; i < usable_region_count; ++i) {
        uint64_t end = saturated_region_end(usable_regions[i].base,
                                            usable_regions[i].length);
        if (address >= usable_regions[i].base && frame_end <= end) {
            return true;
        }
    }
    return false;
}

static bool frame_is_reserved(uint64_t address) {
    return (address & (FRAME_SIZE - 1U)) == 0 &&
           address / FRAME_SIZE < frame_count &&
           test_reserved_frame((size_t)(address / FRAME_SIZE));
}

static bool find_heap_region(uintptr_t metadata_start, uintptr_t metadata_end,
                             uintptr_t *region_end_out) {
    for (size_t i = 0; i < usable_region_count; ++i) {
        uint64_t start = usable_regions[i].base;
        uint64_t end = saturated_region_end(start, usable_regions[i].length);
        if ((uint64_t)metadata_start >= start &&
            (uint64_t)metadata_end <= end) {
            if (end > UINT32_MAX) {
                end = UINT32_MAX;
            }

            for (size_t reserved = 0; reserved < reserved_region_count;
                 ++reserved) {
                uint64_t reserved_start = reserved_regions[reserved].base;
                uint64_t reserved_end = saturated_region_end(
                    reserved_start, reserved_regions[reserved].length);
                if (reserved_start < metadata_end &&
                    reserved_end > metadata_start) {
                    return false;
                }
                if (reserved_start >= metadata_end && reserved_start < end) {
                    end = ALIGN_DOWN(reserved_start, 16U);
                }
            }
            *region_end_out = (uintptr_t)end;
            return metadata_end < *region_end_out;
        }
    }
    return false;
}

int initialize_memory_system(void) {
    if (usable_region_count == 0 || total_memory == 0) {
        printf("Error: no usable Multiboot memory map was provided.\n");
        return -1;
    }

    uint64_t highest_address = 0;
    for (size_t i = 0; i < usable_region_count; ++i) {
        uint64_t end = saturated_region_end(usable_regions[i].base,
                                            usable_regions[i].length);
        if (end > highest_address) {
            highest_address = end;
        }
    }
    if (highest_address > PHYSICAL_MEMORY_LIMIT) {
        highest_address = PHYSICAL_MEMORY_LIMIT;
    }

    frame_count = (size_t)((highest_address + FRAME_SIZE - 1U) / FRAME_SIZE);
    frame_bitmap_size = ALIGN_UP((frame_count + 7U) / 8U, 16U);

    uintptr_t bitmap_start = ALIGN_UP((uintptr_t)&_stack_end, 16U);
    if (frame_bitmap_size > (UINTPTR_MAX - bitmap_start) / 2U) {
        printf("Error: physical-frame bitmaps overflow the address space.\n");
        frame_count = 0;
        return -1;
    }
    uintptr_t bitmap_end = ALIGN_UP(bitmap_start + frame_bitmap_size * 2U,
                                    16U);
    uintptr_t selected_region_end = 0;
    if (bitmap_end < bitmap_start ||
        !find_heap_region(bitmap_start, bitmap_end, &selected_region_end) ||
        selected_region_end - bitmap_end <= sizeof(memory_block) + MIN_ALLOCATION) {
        printf("Error: no usable low-memory region is large enough for the heap.\n");
        frame_count = 0;
        return -1;
    }

    frame_bitmap = (uint8_t*)bitmap_start;
    frame_reserved_bitmap = frame_bitmap + frame_bitmap_size;
    memset(frame_bitmap, 0xFF, frame_bitmap_size);
    memset(frame_reserved_bitmap, 0, frame_bitmap_size);

    /* Start from "all used" and release only complete pages described as usable. */
    for (size_t i = 0; i < usable_region_count; ++i) {
        uint64_t start = usable_regions[i].base;
        uint64_t end = saturated_region_end(start, usable_regions[i].length);
        if (start >= PHYSICAL_MEMORY_LIMIT) {
            continue;
        }
        if (end > PHYSICAL_MEMORY_LIMIT) {
            end = PHYSICAL_MEMORY_LIMIT;
        }

        size_t first = (size_t)((start + FRAME_SIZE - 1U) / FRAME_SIZE);
        size_t last = (size_t)(end / FRAME_SIZE);
        if (last > frame_count) {
            last = frame_count;
        }
        for (size_t frame = first; frame < last; ++frame) {
            if (test_frame(frame)) {
                clear_frame(frame);
                ++managed_frame_count;
                ++free_frame_count;
            }
        }
    }

    heap_begin = bitmap_end;
    uintptr_t desired_heap_end = bitmap_end;
    if (INITIAL_HEAP_SIZE <= UINTPTR_MAX - desired_heap_end) {
        desired_heap_end += INITIAL_HEAP_SIZE;
    } else {
        desired_heap_end = UINTPTR_MAX;
    }
    if (desired_heap_end > selected_region_end) {
        desired_heap_end = selected_region_end;
    }
    heap_limit = ALIGN_DOWN(desired_heap_end, PAGE_SIZE);
    if (heap_limit <= heap_begin + sizeof(memory_block) + MIN_ALLOCATION) {
        printf("Error: initial kernel heap is too small.\n");
        frame_count = 0;
        return -1;
    }

    /* Protect firmware/kernel/bitmap/heap and boot reservations. */
    reserve_frame_range(0, heap_limit);
    for (size_t i = 0; i < reserved_region_count; ++i) {
        reserve_frame_range(reserved_regions[i].base, reserved_regions[i].length);
    }

    free_list = (memory_block*)heap_begin;
    free_list->size = ALIGN_DOWN(heap_limit - heap_begin - sizeof(memory_block), 16U);
    free_list->next = NULL;
    free_list->magic = BLOCK_MAGIC;
    free_list->free = 1;
    heap_arena_count = 1;
    heap_backing_bytes = heap_limit - heap_begin;
    memory_initialized = true;

    printf("Physical memory: %llu MiB detected, %llu MiB managed, "
           "%llu MiB free\n",
           total_memory / 1024U / 1024U,
           ((uint64_t)managed_frame_count * FRAME_SIZE) / 1024U / 1024U,
           ((uint64_t)free_frame_count * FRAME_SIZE) / 1024U / 1024U);
    printf("Frame bitmaps: %p - %p (%u bytes each, %u address frames)\n",
           frame_bitmap, (void*)bitmap_end,
           (unsigned int)frame_bitmap_size, (unsigned int)frame_count);
    printf("Initial heap arena: %p - %p (%u KiB)\n",
           (void*)heap_begin, (void*)heap_limit,
           (unsigned int)((heap_limit - heap_begin) / 1024U));
    return 0;
}

static size_t allocate_frame_from(size_t minimum_address, bool report_failure) {
    uint32_t flags = spinlock_acquire_irq(&frame_lock);
    if (!memory_initialized) {
        spinlock_release_irq(&frame_lock, flags);
        return 0;
    }

    if (minimum_address > SIZE_MAX - (FRAME_SIZE - 1U)) {
        spinlock_release_irq(&frame_lock, flags);
        return 0;
    }
    size_t first = ALIGN_UP(minimum_address, FRAME_SIZE) / FRAME_SIZE;
    if (first == 0) first = 1;
    size_t start = frame_search_hint;
    if (start < first || start >= frame_count) start = first;
    for (unsigned int pass = 0; pass < 2U; ++pass) {
        size_t begin = pass == 0U ? start : first;
        size_t end = pass == 0U ? frame_count : start;
        for (size_t frame = begin; frame < end; ++frame) {
            if (!test_frame(frame)) {
                set_frame(frame);
                if (free_frame_count != 0) --free_frame_count;
                ++allocated_frame_count;
                frame_search_hint = frame + 1U;
                if (frame_search_hint >= frame_count) frame_search_hint = 1U;
                spinlock_release_irq(&frame_lock, flags);
                return frame * FRAME_SIZE;
            }
        }
        if (start == first) break;
    }

    spinlock_release_irq(&frame_lock, flags);
    if (report_failure) {
        printf("[CRITICAL] Physical frame allocation failed "
               "(%u free of %u managed frames).\n",
               (unsigned int)free_frame_count,
               (unsigned int)managed_frame_count);
    }
    return 0;
}

size_t allocate_frame(void) {
    return allocate_frame_from(FRAME_SIZE, true);
}

size_t allocate_frame_at_or_above(size_t minimum_address) {
    return allocate_frame_from(minimum_address, false);
}

void free_frame(size_t addr) {
    if ((addr & (FRAME_SIZE - 1U)) != 0 || addr == 0) {
        return;
    }

    uint32_t flags = spinlock_acquire_irq(&frame_lock);
    size_t frame = addr / FRAME_SIZE;
    if (frame_index_valid(frame) && address_is_usable(addr) &&
        !frame_is_reserved(addr) && test_frame(frame)) {
        clear_frame(frame);
        if (allocated_frame_count != 0) --allocated_frame_count;
        ++free_frame_count;
        if (frame < frame_search_hint || frame_search_hint >= frame_count) {
            frame_search_hint = frame;
        }
    }
    spinlock_release_irq(&frame_lock, flags);
}

static bool reserve_frame_run_locked(size_t begin, size_t end, size_t count,
                                     size_t *run_start_out) {
    size_t run_start = 0;
    size_t run_length = 0;
    for (size_t frame = begin; frame < end; ++frame) {
        if (!test_frame(frame)) {
            if (run_length == 0) run_start = frame;
            if (++run_length == count) {
                *run_start_out = run_start;
                return true;
            }
        } else {
            run_length = 0;
        }
    }
    return false;
}

static uintptr_t reserve_contiguous_frames(size_t count) {
    if (count == 0 || count > frame_count) return 0;
    uint32_t flags = spinlock_acquire_irq(&frame_lock);
    if (!memory_initialized || count > free_frame_count) {
        spinlock_release_irq(&frame_lock, flags);
        return 0;
    }

    size_t start = frame_search_hint;
    if (start < 1U || start >= frame_count) start = 1U;
    size_t run_start = 0;
    bool found = reserve_frame_run_locked(start, frame_count, count,
                                          &run_start);
    if (!found && start != 1U) {
        found = reserve_frame_run_locked(1U, start, count, &run_start);
    }
    if (found) {
        for (size_t item = run_start; item < run_start + count; ++item) {
            set_frame(item);
            set_reserved_frame(item);
        }
        free_frame_count -= count;
        reserved_frame_count += count;
        frame_search_hint = run_start + count;
        if (frame_search_hint >= frame_count) frame_search_hint = 1U;
        spinlock_release_irq(&frame_lock, flags);
        return run_start * FRAME_SIZE;
    }

    spinlock_release_irq(&frame_lock, flags);
    return 0;
}

static bool blocks_are_adjacent(const memory_block *left,
                                const memory_block *right) {
    if (left == NULL || right == NULL) return false;
    uintptr_t left_address = (uintptr_t)left;
    if (left_address > UINTPTR_MAX - sizeof(memory_block)) return false;
    uintptr_t payload = left_address + sizeof(memory_block);
    return left->size <= UINTPTR_MAX - payload &&
           payload + left->size == (uintptr_t)right;
}

static void split_block(memory_block *block, size_t size) {
    size_t remainder = block->size - size;
    if (remainder < sizeof(memory_block) + MIN_ALLOCATION) return;
    memory_block *new_block = (memory_block*)((uint8_t*)block +
                                              sizeof(memory_block) + size);
    new_block->size = remainder - sizeof(memory_block);
    new_block->next = block->next;
    new_block->magic = BLOCK_MAGIC;
    new_block->free = 1;
    block->size = size;
    block->next = new_block;
}

static void insert_heap_block(memory_block *block) {
    memory_block **link = &free_list;
    memory_block *previous = NULL;
    while (*link != NULL && (uintptr_t)*link < (uintptr_t)block) {
        previous = *link;
        link = &(*link)->next;
    }
    block->next = *link;
    *link = block;

    if (block->next != NULL && block->next->free &&
        block->next->magic == BLOCK_MAGIC &&
        blocks_are_adjacent(block, block->next)) {
        block->size += sizeof(memory_block) + block->next->size;
        block->next = block->next->next;
    }
    if (previous != NULL && previous->free &&
        previous->magic == BLOCK_MAGIC &&
        blocks_are_adjacent(previous, block)) {
        previous->size += sizeof(memory_block) + block->size;
        previous->next = block->next;
    }
}

/* Called with heap_lock held.  The frame allocator pins a physically
 * contiguous arena inside the shared 1-GiB direct map, so the heap can grow
 * without requiring a second virtual-address allocator. */
static bool extend_heap_locked(size_t requested_size) {
    if (requested_size > SIZE_MAX - sizeof(memory_block)) return false;
    size_t arena_size = requested_size + sizeof(memory_block);
    if (arena_size < HEAP_GROW_CHUNK) arena_size = HEAP_GROW_CHUNK;
    if (arena_size > SIZE_MAX - (FRAME_SIZE - 1U)) return false;
    arena_size = ALIGN_UP(arena_size, FRAME_SIZE);

    uintptr_t arena_begin = reserve_contiguous_frames(arena_size / FRAME_SIZE);
    if (arena_begin == 0 || arena_begin > UINTPTR_MAX - arena_size) return false;

    ++heap_arena_count;
    heap_backing_bytes += arena_size;

    memory_block *block = (memory_block*)arena_begin;
    block->size = arena_size - sizeof(memory_block);
    block->next = NULL;
    block->magic = BLOCK_MAGIC;
    block->free = 1;
    insert_heap_block(block);
    return true;
}

static memory_block *find_allocated_block(void *ptr) {
    for (memory_block *block = free_list; block != NULL; block = block->next) {
        if (block->magic != BLOCK_MAGIC) {
            return NULL;
        }
        if ((void*)((uint8_t*)block + sizeof(memory_block)) == ptr) {
            return block;
        }
    }
    return NULL;
}

void *k_malloc(size_t size) {
    if (!memory_initialized || size == 0 || size > SIZE_MAX - 15U) {
        return NULL;
    }
    size = ALIGN_UP(size, 16U);

    uint32_t flags = spinlock_acquire_irq(&heap_lock);
    for (unsigned int attempt = 0; attempt < 2U; ++attempt) {
        for (memory_block *current = free_list; current != NULL;
             current = current->next) {
            if (current->magic != BLOCK_MAGIC) {
                spinlock_release_irq(&heap_lock, flags);
                printf("[CRITICAL] Heap metadata is corrupt at %p.\n", current);
                return NULL;
            }
            if (!current->free || current->size < size) continue;

            split_block(current, size);
            current->free = 0;
            void *result = (uint8_t*)current + sizeof(memory_block);
            spinlock_release_irq(&heap_lock, flags);
            return result;
        }
        if (!extend_heap_locked(size)) break;
    }

    spinlock_release_irq(&heap_lock, flags);
    printf("Out of kernel heap memory (requested %u bytes).\n", (unsigned int)size);
    return NULL;
}

void k_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    if (!memory_initialized || ((uintptr_t)ptr & 0xFU) != 0) {
        printf("Warning: rejected invalid heap pointer %p.\n", ptr);
        return;
    }

    uint32_t flags = spinlock_acquire_irq(&heap_lock);
    memory_block *block = find_allocated_block(ptr);
    if (block == NULL || block->free) {
        spinlock_release_irq(&heap_lock, flags);
        printf("Warning: rejected invalid or duplicate free at %p.\n", ptr);
        return;
    }

    block->free = 1;
    if (block->next != NULL && block->next->free &&
        block->next->magic == BLOCK_MAGIC &&
        blocks_are_adjacent(block, block->next)) {
        block->size += sizeof(memory_block) + block->next->size;
        block->next = block->next->next;
    }

    memory_block *previous = NULL;
    for (memory_block *current = free_list;
         current != NULL && current != block; current = current->next) {
        previous = current;
    }
    if (previous != NULL && previous->free &&
        blocks_are_adjacent(previous, block)) {
        previous->size += sizeof(memory_block) + block->size;
        previous->next = block->next;
    }
    spinlock_release_irq(&heap_lock, flags);
}

void *k_realloc(void *ptr, size_t new_size) {
    if (ptr == NULL) {
        return k_malloc(new_size);
    }
    if (new_size == 0) {
        k_free(ptr);
        return NULL;
    }
    if (new_size > SIZE_MAX - 15U) return NULL;
    size_t aligned_size = ALIGN_UP(new_size, 16U);

    uint32_t flags = spinlock_acquire_irq(&heap_lock);
    memory_block *block = find_allocated_block(ptr);
    if (block == NULL || block->free) {
        spinlock_release_irq(&heap_lock, flags);
        return NULL;
    }
    size_t old_size = block->size;
    if (aligned_size <= old_size) {
        spinlock_release_irq(&heap_lock, flags);
        return ptr;
    }

    memory_block *next = block->next;
    if (next != NULL && next->magic == BLOCK_MAGIC && next->free &&
        blocks_are_adjacent(block, next) &&
        old_size + sizeof(memory_block) + next->size >= aligned_size) {
        block->size += sizeof(memory_block) + next->size;
        block->next = next->next;
        split_block(block, aligned_size);
        spinlock_release_irq(&heap_lock, flags);
        return ptr;
    }
    spinlock_release_irq(&heap_lock, flags);

    void *new_ptr = k_malloc(aligned_size);
    if (new_ptr == NULL) {
        return NULL;
    }
    memcpy(new_ptr, ptr, old_size);
    k_free(ptr);
    return new_ptr;
}

void memory_get_stats(memory_stats_t *stats) {
    if (stats == NULL) return;
    memset(stats, 0, sizeof(*stats));
    stats->version = MEMORY_STATS_VERSION;
    stats->struct_size = sizeof(*stats);
    stats->detected_usable_bytes = total_memory;

    uint32_t flags = spinlock_acquire_irq(&frame_lock);
    stats->managed_bytes = (uint64_t)managed_frame_count * FRAME_SIZE;
    stats->reserved_bytes = (uint64_t)reserved_frame_count * FRAME_SIZE;
    stats->allocated_frame_bytes =
        (uint64_t)allocated_frame_count * FRAME_SIZE;
    stats->free_frame_bytes = (uint64_t)free_frame_count * FRAME_SIZE;
    spinlock_release_irq(&frame_lock, flags);

    flags = spinlock_acquire_irq(&heap_lock);
    stats->heap_capacity_bytes = heap_backing_bytes;
    stats->heap_arena_count = heap_arena_count;
    for (memory_block *block = free_list; block != NULL; block = block->next) {
        if (block->magic != BLOCK_MAGIC) break;
        if (block->free) {
            stats->heap_free_bytes += block->size;
            if (block->size > stats->heap_largest_free_block) {
                stats->heap_largest_free_block = block->size;
            }
        } else {
            stats->heap_used_bytes += block->size;
        }
    }
    spinlock_release_irq(&heap_lock, flags);
}

/* Basic allocator/libc smoke tests used by the boot diagnostics. */
static void print_test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
}

void test_memory(void) {
    unsigned int passed = 0;
    unsigned int total = 8;

    void *a = k_malloc(32);
    void *b = k_malloc(64);
    bool allocation = a != NULL && b != NULL && a != b;
    print_test_result("Allocation", allocation);
    passed += allocation;

    void *grown = NULL;
    if (a != NULL) {
        memset(a, 0x5A, 32);
        grown = k_realloc(a, 96);
        if (grown != NULL) {
            a = grown;
        }
    }
    bool reallocation = grown != NULL && ((uint8_t*)a)[0] == 0x5A;
    print_test_result("Reallocation", reallocation);
    passed += reallocation;

    char source[10] = "123456789";
    char destination[10];
    bool copy = memcpy(destination, source, sizeof(source)) == destination &&
                memcmp(destination, source, sizeof(source)) == 0;
    print_test_result("Memcpy", copy);
    passed += copy;

    char overlap[20] = "123456789";
    memmove(overlap + 4, overlap, 10);
    bool move = strcmp(overlap, "1234123456789") == 0;
    print_test_result("Memmove overlap", move);
    passed += move;

    k_free(a);
    k_free(b);
    void *reused = k_malloc(32);
    bool reuse = reused != NULL;
    print_test_result("Free/reuse", reuse);
    passed += reuse;
    k_free(reused);

    size_t high_frame = allocate_frame_at_or_above(256U * 1024U * 1024U);
    bool high_mapping = high_frame != 0 ||
                        frame_count <= (256U * 1024U * 1024U) / FRAME_SIZE;
    if (high_frame != 0) {
        volatile uint32_t *words = (volatile uint32_t*)high_frame;
        words[0] = 0x48494748U;
        words[(FRAME_SIZE / sizeof(uint32_t)) - 1U] = 0x4D454D21U;
        high_mapping = words[0] == 0x48494748U &&
                       words[(FRAME_SIZE / sizeof(uint32_t)) - 1U] ==
                           0x4D454D21U;
        free_frame(high_frame);
    }
    print_test_result("High-frame direct map", high_mapping);
    passed += high_mapping;

    memory_stats_t before_growth;
    memory_stats_t after_growth;
    memory_get_stats(&before_growth);
    void *large = k_malloc(INITIAL_HEAP_SIZE);
    if (large != NULL) {
        ((uint8_t*)large)[0] = 0xA5U;
        ((uint8_t*)large)[INITIAL_HEAP_SIZE - 1U] = 0x5AU;
    }
    memory_get_stats(&after_growth);
    bool heap_growth = large != NULL &&
        ((uint8_t*)large)[0] == 0xA5U &&
        ((uint8_t*)large)[INITIAL_HEAP_SIZE - 1U] == 0x5AU &&
        after_growth.heap_arena_count > before_growth.heap_arena_count &&
        after_growth.heap_capacity_bytes > before_growth.heap_capacity_bytes;
    print_test_result("Heap growth", heap_growth);
    passed += heap_growth;
    k_free(large);

    void *first = k_malloc(128);
    void *middle = k_malloc(256);
    void *last = k_malloc(128);
    k_free(middle);
    void *fragment_reuse = k_malloc(128);
    bool fragmentation = first != NULL && middle != NULL && last != NULL &&
                         fragment_reuse == middle;
    print_test_result("Fragment reuse", fragmentation);
    passed += fragmentation;
    k_free(first);
    k_free(fragment_reuse);
    k_free(last);

    printf("Memory tests: %u/%u passed.\n", passed, total);
    if (passed != total) {
        panic("Kernel memory self-test failed");
    }
}
