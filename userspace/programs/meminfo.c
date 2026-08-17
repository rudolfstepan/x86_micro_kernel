#include "x86os.h"

static void print_unsigned(uint64_t value) {
    char digits[20];
    unsigned int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    while (count != 0U) x86os_putchar(digits[--count]);
}

int main(void) {
    x86os_memory_stats_t stats;
    if (x86os_memory_stats(&stats) != 0 ||
        stats.version != X86OS_MEMORY_STATS_VERSION ||
        stats.struct_size != sizeof(stats)) {
        x86os_puts("Unable to read memory statistics.\n");
        return 1;
    }
    x86os_puts("Detected: ");
    print_unsigned(stats.detected_usable_bytes / 1024U / 1024U);
    x86os_puts(" MiB\nManaged: ");
    print_unsigned(stats.managed_bytes / 1024U / 1024U);
    x86os_puts(" MiB\nReserved: ");
    print_unsigned(stats.reserved_bytes / 1024U / 1024U);
    x86os_puts(" MiB\nFree frames: ");
    print_unsigned(stats.free_frame_bytes / 1024U / 1024U);
    x86os_puts(" MiB\nKernel heap used/free: ");
    print_unsigned(stats.heap_used_bytes / 1024U);
    x86os_puts("/");
    print_unsigned(stats.heap_free_bytes / 1024U);
    x86os_puts(" KiB\nPeak frames/heap: ");
    print_unsigned(stats.peak_allocated_frame_bytes / 1024U);
    x86os_puts("/");
    print_unsigned(stats.peak_heap_used_bytes / 1024U);
    x86os_puts(" KiB\nAllocation failures frame/heap: ");
    print_unsigned(stats.frame_allocation_failures);
    x86os_puts("/");
    print_unsigned(stats.heap_allocation_failures);
    x86os_puts("\n");
    return 0;
}
