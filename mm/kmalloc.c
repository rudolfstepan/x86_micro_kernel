#include "mm/kmalloc.h"
#include "arch/x86/mm/paging.h"
#include "include/lib/spinlock.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"

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

uint64_t total_memory = 0;

static physical_region_t usable_regions[MAX_MEMORY_REGIONS];
static physical_region_t reserved_regions[MAX_MEMORY_REGIONS];
static size_t usable_region_count;
static size_t reserved_region_count;

static uint8_t *frame_bitmap;
static size_t frame_count;
static size_t frame_bitmap_size;

static uintptr_t heap_begin;
static uintptr_t heap_limit;
static memory_block *free_list;
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
    frame_count = 0;
    frame_bitmap_size = 0;
    free_list = NULL;
    heap_begin = 0;
    heap_limit = 0;
    memory_initialized = false;
}

int memory_add_usable_region(uint64_t base, uint64_t length) {
    if (length == 0 || usable_region_count >= MAX_MEMORY_REGIONS) {
        return -1;
    }

    usable_regions[usable_region_count].base = base;
    usable_regions[usable_region_count].length = length;
    usable_region_count++;

    if (UINT64_MAX - total_memory < length) {
        total_memory = UINT64_MAX;
    } else {
        total_memory += length;
    }
    return 0;
}

int memory_reserve_region(uint64_t base, uint64_t length) {
    if (length == 0 || reserved_region_count >= MAX_MEMORY_REGIONS) {
        return -1;
    }

    reserved_regions[reserved_region_count].base = base;
    reserved_regions[reserved_region_count].length = length;
    reserved_region_count++;
    return 0;
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

static void set_frame(size_t frame) {
    if (frame_index_valid(frame)) {
        frame_bitmap[frame / 8U] |= (uint8_t)(1U << (frame % 8U));
    }
}

static void clear_frame(size_t frame) {
    if (frame_index_valid(frame)) {
        frame_bitmap[frame / 8U] &= (uint8_t)~(1U << (frame % 8U));
    }
}

static bool test_frame(size_t frame) {
    return !frame_index_valid(frame) ||
           (frame_bitmap[frame / 8U] & (uint8_t)(1U << (frame % 8U))) != 0;
}

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
        set_frame(frame);
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

static bool ranges_overlap(uint64_t first_base, uint64_t first_length,
                           uint64_t second_base, uint64_t second_length) {
    uint64_t first_end = saturated_region_end(first_base, first_length);
    uint64_t second_end = saturated_region_end(second_base, second_length);
    return first_base < second_end && second_base < first_end;
}

static bool frame_is_reserved(uint64_t address) {
    if (ranges_overlap(address, FRAME_SIZE, 0, heap_limit) ||
        ranges_overlap(address, FRAME_SIZE, KERNEL_PROGRAM_REGION_START,
                       KERNEL_PROGRAM_REGION_SIZE)) {
        return true;
    }

    for (size_t i = 0; i < reserved_region_count; ++i) {
        if (ranges_overlap(address, FRAME_SIZE, reserved_regions[i].base,
                           reserved_regions[i].length)) {
            return true;
        }
    }
    return false;
}

static bool find_heap_region(uintptr_t metadata_start, uintptr_t metadata_end,
                             uintptr_t *region_end_out) {
    for (size_t i = 0; i < usable_region_count; ++i) {
        uint64_t start = usable_regions[i].base;
        uint64_t end = saturated_region_end(start, usable_regions[i].length);
        if ((uint64_t)metadata_start >= start &&
            (uint64_t)metadata_end <= end) {
            if (end > KERNEL_PROGRAM_REGION_START) {
                end = KERNEL_PROGRAM_REGION_START;
            }
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
    uintptr_t bitmap_end = ALIGN_UP(bitmap_start + frame_bitmap_size, 16U);
    uintptr_t selected_region_end = 0;
    if (bitmap_end < bitmap_start ||
        !find_heap_region(bitmap_start, bitmap_end, &selected_region_end) ||
        selected_region_end - bitmap_end <= sizeof(memory_block) + MIN_ALLOCATION) {
        printf("Error: no usable low-memory region is large enough for the heap.\n");
        frame_count = 0;
        return -1;
    }

    frame_bitmap = (uint8_t*)bitmap_start;
    memset(frame_bitmap, 0xFF, frame_bitmap_size);

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
            clear_frame(frame);
        }
    }

    heap_begin = bitmap_end;
    heap_limit = selected_region_end;

    /* Protect firmware/kernel/bitmap/heap, program image, and boot reservations. */
    reserve_frame_range(0, heap_limit);
    reserve_frame_range(KERNEL_PROGRAM_REGION_START,
                        KERNEL_PROGRAM_REGION_SIZE);
    for (size_t i = 0; i < reserved_region_count; ++i) {
        reserve_frame_range(reserved_regions[i].base, reserved_regions[i].length);
    }

    free_list = (memory_block*)heap_begin;
    free_list->size = ALIGN_DOWN(heap_limit - heap_begin - sizeof(memory_block), 16U);
    free_list->next = NULL;
    free_list->magic = BLOCK_MAGIC;
    free_list->free = 1;
    memory_initialized = true;

    printf("Total usable system memory: %llu MB\n", total_memory / 1024U / 1024U);
    printf("Frame bitmap: %p - %p (%u bytes, %u tracked frames)\n",
           frame_bitmap, (void*)(bitmap_start + frame_bitmap_size),
           (unsigned int)frame_bitmap_size, (unsigned int)frame_count);
    printf("Heap range: %p - %p\n", (void*)heap_begin, (void*)heap_limit);
    return 0;
}

size_t allocate_frame(void) {
    uint32_t flags = spinlock_acquire_irq(&frame_lock);
    if (!memory_initialized) {
        spinlock_release_irq(&frame_lock, flags);
        return 0;
    }

    for (size_t frame = 1; frame < frame_count; ++frame) {
        if (!test_frame(frame)) {
            set_frame(frame);
            spinlock_release_irq(&frame_lock, flags);
            return frame * FRAME_SIZE;
        }
    }

    spinlock_release_irq(&frame_lock, flags);
    printf("[CRITICAL] Physical frame allocation failed (%u frames tracked).\n",
           (unsigned int)frame_count);
    return 0;
}

void free_frame(size_t addr) {
    if ((addr & (FRAME_SIZE - 1U)) != 0 || addr == 0) {
        return;
    }

    uint32_t flags = spinlock_acquire_irq(&frame_lock);
    size_t frame = addr / FRAME_SIZE;
    if (frame_index_valid(frame) && address_is_usable(addr) &&
        !frame_is_reserved(addr)) {
        clear_frame(frame);
    }
    spinlock_release_irq(&frame_lock, flags);
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
    for (memory_block *current = free_list; current != NULL; current = current->next) {
        if (current->magic != BLOCK_MAGIC) {
            spinlock_release_irq(&heap_lock, flags);
            printf("[CRITICAL] Heap metadata is corrupt at %p.\n", current);
            return NULL;
        }
        if (!current->free || current->size < size) {
            continue;
        }

        size_t remainder = current->size - size;
        if (remainder >= sizeof(memory_block) + MIN_ALLOCATION) {
            memory_block *new_block = (memory_block*)((uint8_t*)current +
                                                      sizeof(memory_block) + size);
            new_block->size = remainder - sizeof(memory_block);
            new_block->next = current->next;
            new_block->magic = BLOCK_MAGIC;
            new_block->free = 1;
            current->size = size;
            current->next = new_block;
        }
        current->free = 0;

        void *result = (uint8_t*)current + sizeof(memory_block);
        spinlock_release_irq(&heap_lock, flags);
        return result;
    }

    spinlock_release_irq(&heap_lock, flags);
    printf("Out of kernel heap memory (requested %u bytes).\n", (unsigned int)size);
    return NULL;
}

void k_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    if (!memory_initialized || (uintptr_t)ptr < heap_begin + sizeof(memory_block) ||
        (uintptr_t)ptr >= heap_limit || ((uintptr_t)ptr & 0xFU) != 0) {
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
        block->next->magic == BLOCK_MAGIC) {
        block->size += sizeof(memory_block) + block->next->size;
        block->next = block->next->next;
    }

    memory_block *previous = NULL;
    for (memory_block *current = free_list;
         current != NULL && current != block; current = current->next) {
        previous = current;
    }
    if (previous != NULL && previous->free) {
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

    uint32_t flags = spinlock_acquire_irq(&heap_lock);
    memory_block *block = find_allocated_block(ptr);
    if (block == NULL || block->free) {
        spinlock_release_irq(&heap_lock, flags);
        return NULL;
    }
    size_t old_size = block->size;
    spinlock_release_irq(&heap_lock, flags);

    if (new_size <= old_size) {
        return ptr;
    }

    void *new_ptr = k_malloc(new_size);
    if (new_ptr == NULL) {
        return NULL;
    }
    memcpy(new_ptr, ptr, old_size);
    k_free(ptr);
    return new_ptr;
}

/* Basic allocator/libc smoke tests used by the boot diagnostics. */
static void print_test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
}

void test_memory(void) {
    unsigned int passed = 0;
    unsigned int total = 5;

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

    printf("Memory tests: %u/%u passed.\n", passed, total);
}
