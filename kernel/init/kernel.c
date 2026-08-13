/**
 * @file kernel.c
 * @brief REIST OS Main Initialization
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
#include "arch/x86/include/sys.h"
#include "arch/x86/include/mbheader.h"
#include "arch/x86/boot/multiboot_parser.h"
#include "arch/x86/include/interrupt.h"
#include "arch/x86/include/tss.h"
#include "kernel/init/banner.h"
#include "include/kernel/panic.h"
#include "include/kernel/fatal.h"
#include "include/kernel/watchdog.h"
#include "include/kernel/supervisor.h"
#include "include/kernel/storage_safety.h"
#include "include/kernel/output_fence.h"
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
#include "drivers/bus/drives.h"

 // Bus enumeration
 #include "drivers/bus/pci.h"
 void usb_init(void);

// Network subsystem
#include "drivers/net/e1000.h"
#include "drivers/net/ne2000.h"
#include "drivers/net/rtl8139.h"
#include "drivers/net/netstack.h"
#include "drivers/net/netdev.h"

// Filesystems
#include "fs/vfs/filesystem.h"
#include "fs/vfs/vfs.h"
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
    if (!serial_install_rx_irq()) {
        printf("COM1 RX IRQ registration failed; using polling fallback\n");
    }
    
    // Basic hardware
    timer_install(1);  // PIT timer with 1ms ticks
    kb_install();      // Keyboard driver

    // PIT-based delays used by the remaining hardware probes require IRQ
    // delivery.  Floppy detection runs later and skips the controller cleanly
    // when the BIOS reports that no drive is configured.
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
    printf("Hardware initialization complete\n");
}

/**
 * Driver initialization - Block devices and network adapters
 */
static void driver_init(void) {
    // IMPORTANT: Register network drivers ONLY for detected devices
    // Check PCI bus for network cards and register appropriate drivers
    printf("Detecting network hardware...\n");
    
    // Intel E1000: QEMU 82540EM (100E), VMware 82545EM (100F)
    if (pci_device_exists(0x8086, 0x100E) ||
        pci_device_exists(0x8086, 0x100F)) {
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

    if (!storage_safety_init(pit_monotonic_ms())) {
        panic("Unable to initialize REIST storage write supervision");
    }

    // Auto-mount all detected drives
    extern void auto_mount_all_drives(void);
    auto_mount_all_drives();

    //printf("Driver initialization complete\n");
}

/**
 * Display system ready message and status
 */
static void system_ready(void) {
    memory_stats_t memory;
    memory_get_stats(&memory);
    printf("\n=== System Ready ===\n");
    printf("CPU Frequency: %llu Hz\n", cpu_frequency);
    printf("Memory: %llu MiB detected, %llu MiB managed, %llu MiB free\n",
           memory.detected_usable_bytes / 1024U / 1024U,
           memory.managed_bytes / 1024U / 1024U,
           memory.free_frame_bytes / 1024U / 1024U);
    printf("Kernel heap: %llu KiB used, %llu KiB free in %llu arenas\n",
           memory.heap_used_bytes / 1024U,
           memory.heap_free_bytes / 1024U,
           memory.heap_arena_count);
    printf("Drives Detected: %d\n", drive_count);
    
    // Network stack initialization (optional)
    if (netdev_available()) {
        if (!netdev_supervision_init(pit_monotonic_ms())) {
            panic("Unable to supervise network transmit domain");
        }
        netstack_init();
        printf("Network stack initialized on %s\n", netdev_backend_name());
        printf("Requesting LAN configuration via DHCP...\n");
        if (netstack_get_ip_address() == 0) {
            printf("Network link is ready without an IP; use 'getip' to retry\n");
        }
    }
    
    printf("====================\n\n");
}

static bool program_path_for_drive(const drive_t *drive,
                                   const char *filename,
                                   char path[PROCESS_PATH_MAX]) {
    if (drive == NULL || drive->mount_point[0] != '/') return false;
    if (strcmp(drive->mount_point, "/") == 0) {
        if (strlen(filename) + 2U > PROCESS_PATH_MAX) return false;
        path[0] = '/';
        strcpy(path + 1, filename);
    } else {
        size_t length = strlen(drive->mount_point);
        if (length + strlen(filename) + 2U > PROCESS_PATH_MAX) return false;
        strcpy(path, drive->mount_point);
        path[length] = '/';
        strcpy(path + length + 1U, filename);
    }
    vfs_dir_entry_t entry;
    return vfs_stat(path, &entry) == VFS_OK && entry.type == VFS_FILE;
}

static int start_userspace_program(const multiboot1_info_t *boot_info,
                                   const char *filename,
                                   const char *description) {
    drive_type_t preferred_type = DRIVE_TYPE_NONE;
    if ((boot_info->flags & MULTIBOOT1_FLAG_BOOT_DEVICE) != 0) {
        uint8_t bios_drive = (uint8_t)(boot_info->boot_device >> 24);
        preferred_type = bios_drive < 0x80 ? DRIVE_TYPE_FDD : DRIVE_TYPE_ATA;
    }

    for (int pass = 0; pass < 2; ++pass) {
        for (int index = 0; index < drive_count; ++index) {
            drive_t *drive = &detected_drives[index];
            if (drive->mount_point[0] == '\0') continue;
            bool preferred = preferred_type == DRIVE_TYPE_NONE ||
                             drive->type == preferred_type;
            if ((pass == 0) != preferred) continue;

            char program_path[PROCESS_PATH_MAX];
            if (!program_path_for_drive(drive, filename, program_path)) continue;
            const char *arguments[] = {filename};
            /* Publish the READY task and its launch message as one foreground
             * transaction so the child cannot split the serial log line. */
            scheduler_preempt_disable();
            int pid = create_process_for_file_args(
                program_path, 1, arguments, drive->mount_point);
            if (pid < 0) {
                scheduler_preempt_enable();
                continue;
            }

            printf("Starting %s from %s\n", description, program_path);
            scheduler_preempt_enable();
            wait_for_process(pid);
            printf("%s exited.\n", description);
            return 0;
        }
        if (preferred_type == DRIVE_TYPE_NONE) break;
    }
    return -1;
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
 * @param multiboot_magic Multiboot 1 bootloader handoff magic
 * @param multiboot_info Pointer to Multiboot1 info structure
 */
void kernel_main(uint32_t multiboot_magic, const multiboot1_info_t *multiboot_info) {
    if (!scheduler_kernel_context_stack_is_valid()) {
        panic("Static kernel stack guard was not initialized");
    }
    
    // Validate Multiboot magic number
    if (multiboot_magic != MULTIBOOT1_BOOTLOADER_MAGIC) {
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

    /* The stack arena is a virtual window inside the direct map.  Reserve the
     * same physical interval so its non-present guard aliases can never hide
     * frames that the PMM might otherwise allocate. */
    if (memory_reserve_region(KERNEL_STACK_ARENA_BASE,
                              KERNEL_STACK_ARENA_SIZE) != 0) {
        panic("Unable to reserve kernel stack guard arena");
    }
    if (memory_reserve_region(FATAL_CRASH_RECORD_ADDRESS,
                              FATAL_CRASH_RECORD_REGION_SIZE) != 0) {
        panic("Unable to reserve persistent crash record");
    }

    // Initialize kernel memory allocator
    if (initialize_memory_system() != 0) {
        printf("Fatal: kernel memory initialization failed.\n");
        while (1) { asm volatile("hlt"); }
    }

    /* Establish the kernel identity map before any APIC or PCI MMIO access.
     * User processes rely on CR0.PG already being active when scheduled. */
    init_paging();
    test_paging();

#ifdef USE_FRAMEBUFFER
    /* Map the physical framebuffer only after paging exists.  A typical VBE
     * BAR is above the 1-GiB RAM direct map and must be uncached MMIO. */
    if ((multiboot_info->flags & MULTIBOOT1_FLAG_FRAMEBUFFER) != 0 &&
        multiboot_info->framebuffer_addr != 0) {
        multiboot_framebuffer_info_t fb_info = {
            .framebuffer_addr = multiboot_info->framebuffer_addr,
            .framebuffer_pitch = multiboot_info->framebuffer_pitch,
            .framebuffer_width = multiboot_info->framebuffer_width,
            .framebuffer_height = multiboot_info->framebuffer_height,
            .framebuffer_bpp = multiboot_info->framebuffer_bpp,
            .framebuffer_type = multiboot_info->framebuffer_type,
            .red_field_position = multiboot_info->color_info[0],
            .red_mask_size = multiboot_info->color_info[1],
            .green_field_position = multiboot_info->color_info[2],
            .green_mask_size = multiboot_info->color_info[3],
            .blue_field_position = multiboot_info->color_info[4],
            .blue_mask_size = multiboot_info->color_info[5]
        };
        framebuffer_init(&fb_info);
        display_init();
        if (framebuffer_available()) {
            printf("Framebuffer initialized: %ux%ux%u at 0x%x\n",
                   fb_info.framebuffer_width, fb_info.framebuffer_height,
                   fb_info.framebuffer_bpp,
                   (uint32_t)fb_info.framebuffer_addr);
        } else {
            printf("Warning: Unsupported framebuffer, using VGA text mode\n");
        }
    } else {
        display_init();
        printf("Warning: Framebuffer not available, using VGA text mode\n");
    }
#else
    display_init();
#endif

    // Stage 1: Early initialization
    early_init();
    fatal_boot_recover_record();
    supervisor_init();
    output_fence_init();
    if (!output_fence_register(netdev_fence_outputs)) {
        panic("Unable to register the network output fence");
    }
    
    // Stage 2: Hardware initialization
    hardware_init();
    
    // Stage 3: Driver initialization
    driver_init();
    
    // Stage 4: System ready
    system_ready();
    watchdog_init();
    printf("Watchdog: %s\n", watchdog_available() ? "IB700 armed" : "external backend required");
#ifdef REIST_FAULT_INJECTION
    if (fatal_last_crash_record()->magic != FATAL_CRASH_RECORD_MAGIC) {
        printf("REIST_TEST DOUBLE_FAULT_ARMED\n");
        fatal_test_trigger_double_fault();
    }
    printf("REIST_TEST FATAL_RECOVERY_OK\n");
#endif
    if (!supervisor_start_worker()) {
        panic("Unable to start REIST safety supervisor worker");
    }
    printf("BOOT_OK\n");

    /* A real framebuffer prefers the graphical desktop.  VGA boots and any
     * failed/terminated desktop fall back to the userspace shell. */
#ifdef USE_FRAMEBUFFER
    if (framebuffer_available()) {
        if (start_userspace_program(multiboot_info, "DESKTOP.PRG",
                                    "graphical desktop") == 0) {
            printf("Graphical desktop exited; starting shell fallback.\n");
        } else {
            printf("Unable to start DESKTOP.PRG; starting shell fallback.\n");
        }
    }
#endif
    if (start_userspace_program(multiboot_info, "SHELL.PRG",
                                "userspace command interpreter") < 0) {
        printf("Unable to start SHELL.PRG; entering rescue shell.\n");
    }

    // Enter the recovery shell (this never returns)
    command_loop();

    // Should never reach here
    while (1) {
        printf("PANIC: command_loop exited unexpectedly!\n");
        delay_ms(1000);
        asm volatile("hlt");
    }
}
