/**
 * @file kernel.c
 * @brief x86 Microkernel Main Initialization
 * 
 * This is the main entry point for the kernel after bootloader handoff.
 * Provides staged initialization of all kernel subsystems:
 * - Early init: GDT, IDT, ISR, IRQ tables
 * - Hardware: Timers, keyboard, display
 * - Drivers: Block devices, network adapters
 * - Filesystems: FAT32 support
 * - Shell: Command-line interface
 */

#include <stdbool.h>
#include <stdint.h>

// Core kernel headers
#include "arch/x86/sys.h"
#include "arch/x86/mbheader.h"
#include "arch/x86/boot/multiboot_parser.h"
#include "arch/x86/include/interrupt.h"
#include "arch/x86/include/tss.h"
#include "kernel/init/banner.h"
#include "kernel/shell/command.h"
#include "mm/kmalloc.h"

// Timing subsystem
#include "kernel/time/pit.h"
#include "kernel/time/apic.h"

// Process management (currently disabled)
#include "kernel/sched/scheduler.h"
#include "kernel/proc/process.h"

// Character drivers
#include "drivers/char/kb.h"
#include "drivers/char/rtc.h"
#include "drivers/char/io.h"
#include "drivers/char/serial.h"

// Video subsystem
#include "drivers/video/display.h"
#ifdef USE_FRAMEBUFFER
#include "drivers/video/framebuffer.h"
#endif

// Block devices
#include "drivers/block/ata.h"
#include "drivers/block/fdd.h"

 // Bus enumeration
 #include "drivers/bus/pci.h"
 void usb_init(void);

// Network subsystem
#include "drivers/net/e1000.h"
#include "drivers/net/ne2000.h"
#include "drivers/net/rtl8139.h"
#include "drivers/net/netstack.h"

// Filesystems
#include "fs/vfs/filesystem.h"
#include "fs/fat32/fat32.h"

// Standard library
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"

//---------------------------------------------------------------------------------------------
// Global Variables
//---------------------------------------------------------------------------------------------

volatile uint64_t cpu_frequency = 0;  // CPU speed in Hz (calculated at boot)


//---------------------------------------------------------------------------------------------
// CPU Utility Functions
//---------------------------------------------------------------------------------------------

/**
 * Read CPU timestamp counter for frequency calculation
 * @return 64-bit cycle count
 */
static inline uint64_t read_cpu_cycle_counter(void) {
    uint32_t high, low;
    __asm__ __volatile__("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

//---------------------------------------------------------------------------------------------
// Initialization Stages
//---------------------------------------------------------------------------------------------

/**
 * Early initialization - CPU tables and basic hardware
 * Sets up protected mode, interrupts, and basic timing
 * 
 * CRITICAL: Interrupts are disabled during this phase to prevent
 * race conditions and ensure atomic setup of all data structures.
 */
static void early_init(void) {
    // CRITICAL: Disable interrupts during initialization
    irq_disable();
    
    // Initialize serial port early (for debugging and nographic mode)
    serial_init_default();  // Initialize COM1 at 115200 baud
    
    // CPU setup - TSS must be initialized before GDT
    extern char _stack_end;  // From linker script (klink.ld)
    uint32_t kernel_stack = (uint32_t)&_stack_end;
    tss_init(kernel_stack, 0x10);  // ESP0 = kernel stack, SS0 = kernel data (0x10)
    
    gdt_install();  // Global Descriptor Table (includes TSS descriptor)
    idt_install();  // Interrupt Descriptor Table
    isr_install();  // CPU exception handlers (0-31)
    irq_install();  // Hardware interrupt handlers (32-47)
    
    // Basic hardware
    timer_install(1);  // PIT timer with 1ms ticks
    kb_install();      // Keyboard driver
    fdc_init_controller();  // Floppy disk controller
    
    // Re-enable interrupts now that everything is set up
    irq_enable();
    
    printf("Early initialization complete (interrupts enabled)\n");
    printf("TSS initialized with kernel stack at 0x%08X\n", kernel_stack);
}

/**
 * Hardware initialization - Advanced timers and buses
 */
static void hardware_init(void) {
    // Memory subsystem test
    test_memory();
    
    // Advanced timing
    initialize_apic_timer();  // Local APIC timer
    
    // Bus enumeration
    pci_init();  // PCI bus scanning
    usb_init();  // Initialize USB subsystem (probe PCI for HCI)
    // Scheduler interrupt (currently disabled)
    register_interrupt_handler(9, scheduler_interrupt_handler);
    
    printf("Hardware initialization complete\n");
}

/**
 * Driver initialization - Block devices and network adapters
 */
static void driver_init(void) {
    // IMPORTANT: Register network drivers ONLY for detected devices
    // Check PCI bus for network cards and register appropriate drivers
    printf("Detecting network hardware...\n");
    
    // Intel E1000 (vendor: 0x8086, device: 0x100E)
    if (pci_device_exists(0x8086, 0x100E)) {
        printf("  - Intel E1000 detected, registering driver\n");
        e1000_detect();  // Register E1000 driver
    }
    
    // Realtek RTL8139 (vendor: 0x10EC, device: 0x8139)
    if (pci_device_exists(0x10EC, 0x8139)) {
        printf("  - Realtek RTL8139 detected, registering driver\n");
        rtl8139_detect(); // Register RTL8139 driver
    }
    
    // NE2000 compatible (vendor: 0x10EC, device: 0x8029)
    if (pci_device_exists(0x10EC, 0x8029)) {
        printf("  - NE2000 compatible detected, registering driver\n");
        ne2000_detect(); // Register NE2000 driver
    }
    
    // Probe PCI devices and initialize registered drivers
    //printf("Initializing network drivers...\n");
    pci_probe_drivers();
    
    // Enable hardware interrupts
    __asm__ __volatile__("sti");
    
    // Calculate CPU frequency for timing calibration
    uint64_t start_cycles = read_cpu_cycle_counter();
    pit_delay(1000);  // 1 second hardware delay
    uint64_t end_cycles = read_cpu_cycle_counter();
    cpu_frequency = end_cycles - start_cycles;
    
    // Detect storage devices
    ata_detect_drives();  // IDE/SATA hard drives

    // Detect floppy drives
    fdd_detect_drives();  // Floppy disk drives

    // Auto-mount all detected drives
    extern void auto_mount_all_drives(void);
    auto_mount_all_drives();

    //printf("Driver initialization complete\n");
}

/**
 * Display system ready message and status
 */
static void system_ready(void) {
    printf("\n=== System Ready ===\n");
    printf("CPU Frequency: %llu Hz\n", cpu_frequency);
    printf("Total Memory: %llu MB\n", total_memory / 1024 / 1024);
    printf("Drives Detected: %d\n", drive_count);
    
    // Network stack initialization (optional)
    if (e1000_is_initialized()) {
        netstack_init();
        printf("Network stack initialized\n");
    }
    
    printf("====================\n\n");
}

//---------------------------------------------------------------------------------------------
// Kernel Main Entry Point
//---------------------------------------------------------------------------------------------

/**
 * Kernel main initialization and command loop
 * 
 * Called by bootloader after setting up protected mode.
 * Validates Multiboot information, initializes all subsystems,
 * and enters the interactive shell command loop.
 * 
 * @param multiboot_magic Magic number from bootloader (must be 0x36d76289 for MB1)
 * @param multiboot_info Pointer to Multiboot1 info structure
 */
void kernel_main(uint32_t multiboot_magic, const multiboot1_info_t *multiboot_info) {
    
    // Validate Multiboot magic number
    if (multiboot_magic != 0x36d76289) {
        printf("Error: Invalid Multiboot magic number: 0x%x\n", multiboot_magic);
        while (1) { asm volatile("hlt"); }
    }

    // Validate multiboot info structure
    if (multiboot_info == NULL) {
        printf("Error: Multiboot information structure is NULL.\n");
        while (1) { asm volatile("hlt"); }
    }

    // Parse bootloader-provided information
    parse_multiboot1_info(multiboot_info);

#ifdef USE_FRAMEBUFFER
    // Initialize framebuffer if available (for graphical boot)
    if (multiboot_info->framebuffer_addr != 0) {
        multiboot_framebuffer_info_t fb_info = {
            .framebuffer_addr = (uint32_t)multiboot_info->framebuffer_addr,
            .framebuffer_pitch = multiboot_info->framebuffer_pitch,
            .framebuffer_width = multiboot_info->framebuffer_width,
            .framebuffer_height = multiboot_info->framebuffer_height,
            .framebuffer_bpp = multiboot_info->framebuffer_bpp,
            .framebuffer_type = multiboot_info->framebuffer_type
        };
        framebuffer_init(&fb_info);
        display_init();
        printf("Framebuffer initialized: %ux%ux%u at 0x%x\n",
               fb_info.framebuffer_width, fb_info.framebuffer_height,
               fb_info.framebuffer_bpp, fb_info.framebuffer_addr);
    } else {
        printf("Warning: Framebuffer not available, using VGA text mode\n");
    }
#else
    // Standard VGA text mode
    display_init();
#endif

    // Initialize kernel memory allocator
    initialize_memory_system();

    // Stage 1: Early initialization
    early_init();
    
    // Stage 2: Hardware initialization
    hardware_init();
    
    // Stage 3: Driver initialization
    driver_init();
    
    // Stage 4: System ready
    system_ready();

    // Enter interactive shell (this never returns)
    command_loop();

    // Should never reach here
    while (1) {
        printf("PANIC: command_loop exited unexpectedly!\n");
        delay_ms(1000);
        asm volatile("hlt");
    }
}
