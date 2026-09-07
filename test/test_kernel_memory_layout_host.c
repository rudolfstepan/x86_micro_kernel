/* Real PMM tests with only the boot-symbol address, IRQ and mapping replaced. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mm/kmalloc.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#define KERNEL_IDENTITY_LIMIT (1024U * 1024U * 1024U)
#define PAGE_SIZE 4096U
#define HOST_STACK_END 0x03400000U
#define _stack_end (*(char *)(uintptr_t)HOST_STACK_END)
typedef int spinlock_t;
#define SPINLOCK_INIT 0
static uint32_t spinlock_acquire_irq(spinlock_t *lock) { (void)lock; return 0; }
static void spinlock_release_irq(spinlock_t *lock, uint32_t flags) { (void)lock; (void)flags; }
/* PRODUCTION */

int main(void) {
    const size_t backing_size = 2U * 1024U * 1024U;
#ifdef _WIN32
    void *backing = VirtualAlloc((void *)(uintptr_t)HOST_STACK_END, backing_size,
                                 MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void *backing = mmap((void *)(uintptr_t)HOST_STACK_END, backing_size,
                        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    assert(backing == (void *)(uintptr_t)HOST_STACK_END);
    for (unsigned profile = 0; profile < 2U; ++profile) {
        const uint32_t managed_end = profile ? 1024U*1024U*1024U : 128U*1024U*1024U;
        memory_map_reset();
        assert(memory_add_usable_region(0, managed_end) == 0);
        assert(memory_reserve_region(80U*1024U*1024U, 4U*1024U*1024U) == 0);
        assert(initialize_memory_system() == 0);
        assert((uintptr_t)frame_bitmap >= HOST_STACK_END);
        assert(heap_begin >= (uintptr_t)frame_reserved_bitmap + frame_bitmap_size);
        assert(heap_limit > heap_begin && heap_limit <= HOST_STACK_END + backing_size);
        for (size_t address = 0; address < heap_limit; address += FRAME_SIZE) {
            assert(test_frame(address / FRAME_SIZE));
            assert(frame_is_reserved(address));
        }
        size_t initial = free_frame_count;
        free_frame(HOST_STACK_END - FRAME_SIZE); /* cannot release BSS/stack */
        free_frame(80U*1024U*1024U);             /* cannot release firmware */
        assert(free_frame_count == initial);
        size_t count = 0;
        size_t frame;
        while ((frame = allocate_user_frame()) != 0) {
            assert(++count <= managed_end / FRAME_SIZE);
            assert(frame >= heap_limit && frame < managed_end);
            assert(frame < 80U*1024U*1024U || frame >= 84U*1024U*1024U);
            assert(!frame_is_reserved(frame));
        }
        assert(count > 0 && free_frame_count == managed_frame_count / 16U);
        frame = allocate_frame(); /* recovery reserve is still accessible to kernel */
        assert(frame && free_frame_count == managed_frame_count / 16U - 1U);
        free_frame(frame);
        for (size_t address = heap_limit; address < managed_end; address += FRAME_SIZE)
            free_frame(address);
        assert(free_frame_count == initial && allocated_frame_count == 0);
    }
    /* Firmware overlapping bitmap/initial heap must reject before any write. */
    memory_map_reset();
    assert(memory_add_usable_region(0, 128U*1024U*1024U) == 0);
    assert(memory_reserve_region(HOST_STACK_END, FRAME_SIZE) == 0);
    memset(backing, 0x5a, backing_size);
    assert(initialize_memory_system() == -1 && !memory_initialized);
    for (size_t i = 0; i < backing_size; ++i) assert(((uint8_t *)backing)[i] == 0x5a);
#ifdef _WIN32
    assert(VirtualFree(backing, 0, MEM_RELEASE));
#else
    assert(munmap(backing, backing_size) == 0);
#endif
    puts("KERNEL_LAYOUT_HOST_OK");
    return 0;
}
