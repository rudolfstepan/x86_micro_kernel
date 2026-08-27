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
#include "arch/x86/platform/acpi.h"
#include "arch/x86/include/interrupt.h"
#include "arch/x86/include/cpu_local.h"
#include "arch/x86/include/smp.h"
#include "arch/x86/include/tss.h"
#include "kernel/init/banner.h"
#include "include/kernel/panic.h"
#include "include/kernel/fatal.h"
#include "include/kernel/watchdog.h"
#include "include/kernel/supervisor.h"
#include "include/kernel/storage_safety.h"
#include "include/kernel/storage_handover.h"
#include "include/kernel/storage_service.h"
#include "include/kernel/storage_maintenance.h"
#include "include/kernel/boot_health.h"
#include "include/kernel/handover.h"
#include "include/kernel/handover_replica.h"
#include "include/kernel/handover_serial_backend.h"
#include "include/kernel/filesystem_safety.h"
#include "include/kernel/output_fence.h"
#include "include/kernel/ipc.h"
#include "include/kernel/critical_object.h"
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
#include "drivers/video/display_control.h"
#include "include/kernel/device_domain.h"
#include "kernel/init/audio_device_profile.h"
#include "kernel/init/video_device_profile.h"
#ifdef USE_FRAMEBUFFER
#include "drivers/video/framebuffer.h"
#endif

// Block devices
#include "drivers/block/ata.h"
#include "drivers/block/ahci.h"
#include "drivers/block/fdd.h"
#include "drivers/block/block_device.h"
#include "drivers/block/partition.h"
#include "drivers/bus/drives.h"

 // Bus enumeration
 #include "drivers/bus/pci.h"
 void usb_init(void);

// Network subsystem
#include "drivers/net/e1000.h"
#include "drivers/net/ne2000.h"
#include "drivers/net/rtl8168.h"
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

#ifdef REIST_DRIVER_DOMAIN_FAULT_INJECTION
static uint32_t driver_fault_recovery_device = UINT32_MAX;
static uint32_t driver_fault_reset_device = UINT32_MAX;
#endif
static audio_device_profile_info_t audio_device_info;
static bool audio_device_available;
static video_device_profile_info_t video_device_info;
static bool video_device_available;
static const drive_t *smp_storage_probe_drive;
static uint8_t smp_storage_probe_reference[512];
#define SMP_INTEGRITY_PROBE_VERSION 1U
#define SMP_INTEGRITY_PROBE_COOKIE 0x534D5000U
typedef struct {
    uint32_t cpu_index;
    uint32_t cookie;
} smp_integrity_probe_payload_t;
static critical_object_t smp_integrity_shared;
static critical_object_t smp_integrity_private[X86_SMP_MAX_CPUS];
static volatile uint32_t smp_integrity_arrived_mask;
static uint32_t smp_integrity_expected_mask;

static bool smp_integrity_probe_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(smp_integrity_probe_payload_t))
        return false;
    const smp_integrity_probe_payload_t *probe = payload;
    return probe->cpu_index < X86_SMP_MAX_CPUS &&
           probe->cookie == (SMP_INTEGRITY_PROBE_COOKIE ^ probe->cpu_index);
}

static bool smp_integrity_probe_worker(uint32_t cpu_index) {
    smp_integrity_probe_payload_t payload = {
        .cpu_index = cpu_index,
        .cookie = SMP_INTEGRITY_PROBE_COOKIE ^ cpu_index,
    };
    smp_integrity_probe_payload_t observed = {0U, 0U};
    size_t observed_length = 0U;
    if (critical_object_update(&smp_integrity_shared,
                               SMP_INTEGRITY_PROBE_VERSION, &payload,
                               sizeof(payload), smp_integrity_probe_valid) != 0)
        return false;
    critical_read_result_t result = critical_object_read(
        &smp_integrity_shared, SMP_INTEGRITY_PROBE_VERSION, &observed,
        sizeof(observed), &observed_length, smp_integrity_probe_valid);
    if (result < CRITICAL_READ_OK ||
        !smp_integrity_probe_valid(&observed, observed_length)) return false;

    critical_object_t *private_object = &smp_integrity_private[cpu_index];
    private_object->primary.words[CRITICAL_OBJECT_METADATA_WORDS] ^= 1U << 7U;
    result = critical_object_read(
        private_object, SMP_INTEGRITY_PROBE_VERSION, &observed,
        sizeof(observed), &observed_length, smp_integrity_probe_valid);
    if (result != CRITICAL_READ_CORRECTED || observed.cpu_index != cpu_index)
        return false;

    private_object->primary.crc32 ^= 1U;
    result = critical_object_read(
        private_object, SMP_INTEGRITY_PROBE_VERSION, &observed,
        sizeof(observed), &observed_length, smp_integrity_probe_valid);
    if (result != CRITICAL_READ_RECOVERED || observed.cpu_index != cpu_index)
        return false;

    private_object->primary.crc32 ^= 1U;
    private_object->shadow.crc32 ^= 2U;
    return critical_object_read(
               private_object, SMP_INTEGRITY_PROBE_VERSION, &observed,
               sizeof(observed), &observed_length,
               smp_integrity_probe_valid) == CRITICAL_READ_UNCORRECTABLE;
}

static bool smp_storage_probe_worker(uint32_t cpu_index) {
    if (cpu_index == 0U || cpu_index >= X86_SMP_MAX_CPUS ||
        smp_storage_probe_drive == NULL) return false;
    uint8_t sector[512];
    if (block_device_read_sector(smp_storage_probe_drive, 0U, sector) !=
            BLOCK_DEVICE_OK ||
        memcmp(sector, smp_storage_probe_reference, sizeof(sector)) != 0)
        return false;
    __sync_fetch_and_or(&smp_integrity_arrived_mask, 1U << cpu_index);
    uint64_t now_ms = pit_monotonic_ms();
    uint64_t deadline = UINT64_MAX - now_ms < 1000U
        ? UINT64_MAX : now_ms + 1000U;
    while ((smp_integrity_arrived_mask & smp_integrity_expected_mask) !=
           smp_integrity_expected_mask) {
        if (pit_monotonic_ms() >= deadline) return false;
        pit_delay(1U);
    }
    return smp_integrity_probe_worker(cpu_index);
}

static bool smp_storage_probe_prepare(void) {
    if (current_drive == NULL || current_drive->sectors == 0U ||
        block_device_read_sector(current_drive, 0U,
                                 smp_storage_probe_reference) !=
            BLOCK_DEVICE_OK) return false;
    x86_smp_status_t smp_status;
    x86_smp_status(&smp_status);
    if (smp_status.online_cpu_count == 0U ||
        smp_status.online_cpu_count > X86_SMP_MAX_CPUS) return false;
    smp_integrity_arrived_mask = 0U;
    smp_integrity_expected_mask =
        ((1U << smp_status.online_cpu_count) - 1U) & ~1U;
    smp_integrity_probe_payload_t initial = {
        .cpu_index = 0U,
        .cookie = SMP_INTEGRITY_PROBE_COOKIE,
    };
    if (critical_object_init(&smp_integrity_shared,
                             SMP_INTEGRITY_PROBE_VERSION, &initial,
                             sizeof(initial)) != 0) return false;
    for (uint32_t cpu = 1U; cpu < X86_SMP_MAX_CPUS; ++cpu) {
        smp_integrity_probe_payload_t private_initial = {
            .cpu_index = cpu,
            .cookie = SMP_INTEGRITY_PROBE_COOKIE ^ cpu,
        };
        if (critical_object_init(&smp_integrity_private[cpu],
                                 SMP_INTEGRITY_PROBE_VERSION,
                                 &private_initial,
                                 sizeof(private_initial)) != 0) return false;
    }
    smp_storage_probe_drive = current_drive;
    __sync_synchronize();
    return x86_smp_set_parallel_probe(smp_storage_probe_worker);
}


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

static void boot_context(const char *phase, const char *component,
                         const char *operation, const char *subject) {
    panic_context_set(phase, component, operation, subject);
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
    if (!serial_default_present()) {
        printf("COM1 unavailable; serial diagnostics disabled\n");
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
    boot_context("hardware-init", "memory", "self-test", "physical memory");
    test_memory();
    
    // Advanced timing
    boot_context("hardware-init", "APIC timer", "initialize", "local APIC");
    initialize_apic_timer();  // Local APIC timer
    boot_context("hardware-init", "SMP", "start", "application processors");
    (void)x86_smp_initialize();
    
    // Bus enumeration
    boot_context("hardware-init", "PCI", "enumerate", "PCI buses");
    pci_init();  // PCI bus scanning
    boot_context("hardware-init", "driver domains", "bootstrap",
                 "fail-closed PCI resource mediator");
    if (!device_domain_bootstrap()) {
        printf("DEVICE_DOMAIN: unavailable; Ring-3 device claims disabled\n");
    } else {
        int video_result = video_device_profile_discover(&video_device_info);
        if (video_result == 1) {
            video_device_available = true;
            printf("REIST_VIDEO DRIVER_PROFILE backend=%u pci=%04X:%04X\n",
                   (unsigned)video_device_info.backend,
                   video_device_info.vendor_id, video_device_info.device_id);
        } else if (video_result < 0) {
            printf("REIST_VIDEO DRIVER_REJECTED result=%d\n", video_result);
        }
        int audio_result = audio_device_profile_discover(&audio_device_info);
        if (audio_result == 1) {
            audio_device_available = true;
            printf("REIST_AUDIO HDA_PROFILE pci=%04X:%04X streams=%u\n",
                   audio_device_info.vendor_id, audio_device_info.device_id,
                   audio_device_info.output_streams);
        } else if (audio_result < 0) {
            printf("REIST_AUDIO HDA_REJECTED result=%d\n", audio_result);
        } else {
            printf("REIST_AUDIO HDA_NOT_PRESENT\n");
        }
    }
#ifdef REIST_DRIVER_DOMAIN_FAULT_INJECTION
    if (device_domain_fault_test_register(
            &driver_fault_recovery_device,
            &driver_fault_reset_device) != 0) {
        panic("Unable to register driver-domain fault fixtures");
    }
#endif
#ifdef REIST_RUNTIME_DEGRADATION_FAULT_INJECTION
    if (!scheduler_policy_degradation_self_test() ||
        !device_domain_irq_storm_self_test()) {
        panic("Runtime degradation guard self-test failed");
    }
    printf("REIST_RUNTIME_DEGRADATION CLOCK_SAFE IRQ_FENCED\n");
#endif
    /* Establish runtime graphics MMIO mappings before process page
     * directories copy the shared high-kernel PDEs.  This does not switch the
     * VGA mode or publish a framebuffer. */
    display_control_prepare();
    boot_context("hardware-init", "USB", "enumerate", "USB controllers");
    usb_init();  // Initialize USB subsystem (probe PCI for HCI)
    printf("Hardware initialization complete\n");
}

/**
 * Driver initialization - Block devices and network adapters
 */
static void driver_init(const multiboot1_info_t *boot_info) {
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

    // Realtek RTL8111G/RTL8168 (vendor: 0x10EC, device: 0x8168)
    if (pci_device_exists(0x10EC, 0x8168)) {
        printf("  - Realtek RTL8111G detected, registering driver\n");
        rtl8168_detect(); // Register RTL8168/8111G driver
    }
    
    // NE2000 compatible (vendor: 0x10EC, device: 0x8029)
    if (pci_device_exists(0x10EC, 0x8029)) {
        printf("  - NE2000 compatible detected, registering driver\n");
        ne2000_detect(); // Register NE2000 driver
    }
    
    // Probe PCI devices and initialize registered drivers
    //printf("Initializing network drivers...\n");
    boot_context("driver-init", "PCI", "probe registered drivers",
                 "network controllers");
    pci_probe_drivers();
    
    // Enable hardware interrupts
    __asm__ __volatile__("sti");
    
    // Calculate CPU frequency for timing calibration
    boot_context("driver-init", "CPU timer", "calibrate", "TSC against PIT");
    uint64_t start_cycles = read_cpu_cycle_counter();
    pit_delay(1000);  // 1 second hardware delay
    uint64_t end_cycles = read_cpu_cycle_counter();
    cpu_frequency = end_cycles - start_cycles;
    
    // Detect storage devices
    boot_context("driver-init", "ATA", "detect", "legacy IDE drives");
    ata_detect_drives();  // IDE/SATA hard drives

    boot_context("driver-init", "AHCI", "initialize", "SATA controllers");
    ahci_init();           // Publish validated SATA resources after ATA scan

    // Detect floppy drives
    boot_context("driver-init", "FDC", "detect", "floppy drives");
    fdd_detect_drives();  // Floppy disk drives

    if (drive_count <= 0) {
        uint32_t ata_summary = ata_probe_diagnostics();
        uint32_t ahci_summary = ahci_probe_diagnostics();
        boot_context("driver-init", "storage probe", "publish",
                     "physical block devices");
        panic_context_set_result(-1, ata_summary, ahci_summary);
        printf("Storage probe failed: ATA=%08X AHCI=%08X\n",
               ata_summary, ahci_summary);
        panic("No physical block storage device detected");
    }

    boot_context("driver-init", "partition", "discover", "block devices");
    partition_discover(); // Publish bounded CRC-validated partition children

    boot_context("storage-init", "storage safety", "initialize",
                 "write supervision");
    if (!storage_safety_init(pit_monotonic_ms())) {
        panic_context_set_result(-1, 0U, 0U);
        panic("Unable to initialize REIST storage write supervision");
    }
    boot_context("storage-init", "storage service", "inventory",
                 "detected media");
    if (!storage_service_inventory_media()) {
        panic_context_set_result(-1, (uint32_t)drive_count, 0U);
        panic("Unable to inventory REIST storage media");
    }
#if defined(REIST_HANDOVER_FAULT_INJECTION) && \
    (REIST_HANDOVER_NODE_ID == 2 || REIST_HANDOVER_NODE_ID == 3)
    if (!storage_handover_hold())
        panic("Unable to hold standby storage outputs");
#endif
    boot_context("storage-init", "output fence", "register", "storage writes");
    if (!output_fence_register(storage_fence_writes)) {
        panic_context_set_result(-1, 0U, 0U);
        panic("Unable to register the storage write fence");
    }

    // Auto-mount all detected drives
    boot_context("filesystem-init", "VFS", "auto-mount",
                 "detected filesystems");
    int boot_floppy_drive = -1;
    if (boot_info != NULL &&
        (boot_info->flags & MULTIBOOT1_FLAG_BOOT_DEVICE) != 0U) {
        uint8_t bios_drive = (uint8_t)(boot_info->boot_device >> 24U);
        if (bios_drive < 0x80U) boot_floppy_drive = bios_drive;
    }
    auto_mount_all_drives(boot_floppy_drive);

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
    
    printf("====================\n\n");
}

static void network_initialize(void) {
    if (netdev_available()) {
        boot_context("network-init", netdev_backend_name(), "supervise",
                     "network transmit domain");
        if (!netdev_supervision_init(pit_monotonic_ms())) {
            panic_context_set_result(-1, 0U, 0U);
            panic("Unable to supervise network transmit domain");
        }
        boot_context("network-init", netdev_backend_name(), "initialize",
                     "network stack");
        netstack_init();
        printf("Network stack initialized on %s\n", netdev_backend_name());
    }
}

static uint64_t kernel_deadline_after(uint64_t now_ms, uint32_t timeout_ms) {
    return UINT64_MAX - now_ms < timeout_ms
        ? UINT64_MAX : now_ms + timeout_ms;
}

static void configure_network_after_service(void) {
    uint64_t ready_deadline = kernel_deadline_after(
        pit_monotonic_ms(), 10000U);
    while (!supervisor_probe_ready() && pit_monotonic_ms() < ready_deadline)
        __asm__ __volatile__("sti; hlt");
    if (!supervisor_probe_ready()) {
        panic_context_set_result(-110, 10000U, 0U);
        panic("REIST Ring-3 service readiness deadline expired");
    }
    if (!netdev_available()) {
        printf("Network hardware unavailable; REIST service ready in local-only mode\n");
        return;
    }
    printf("Waiting for supervised Ring-3 DHCP configuration...\n");
    uint64_t commit_deadline = kernel_deadline_after(
        pit_monotonic_ms(), 6000U);
    while (!netstack_is_configured() && pit_monotonic_ms() < commit_deadline)
        __asm__ __volatile__("sti; hlt");
    if (!netstack_is_configured())
        printf("DHCP did not complete; network remains fail-closed\n");
}

#ifdef REIST_HANDOVER_FAULT_INJECTION
static void test_external_handover_channel(void) {
    const uint32_t lease_ms = 50U;
    uint64_t now = pit_monotonic_ms();
    if (!handover_serial_backend_init() ||
        handover_attach_fence_backend(handover_serial_backend()) != 0)
        panic("Unable to initialize external handover test channel");

#if REIST_HANDOVER_NODE_ID == 1
    if (handover_init(1U, 2U, lease_ms, now) != 0)
        panic("Unable to initialize active handover state");
    handover_replica_state_t state;
    if (!storage_handover_snapshot(1U, 1U, 1U, &state))
        panic("Unable to snapshot active storage state");
    if (handover_replica_init(&state) != 0)
        panic("Unable to initialize active replicated service state");
    for (uint32_t update = 0U; update < 3U; ++update) {
        if (handover_replica_snapshot(&state) != 0 ||
            !handover_serial_send_state(&state))
            panic("Unable to replicate active service state");
        if (update + 1U < 3U) {
            ++state.sequence;
            if (handover_replica_apply(&state) != 0)
                panic("Unable to advance active service state");
            pit_delay(10U);
        }
    }
    printf("REIST_HANDOVER ACTIVE_STATE_SENT\n");
    return;
#elif REIST_HANDOVER_NODE_ID == 2
    if (!handover_serial_send_ready(2U, 1U))
        panic("Unable to announce standby handover readiness");
    handover_replica_state_t state;
    if (!handover_serial_receive_state(&state) || state.source_node != 1U ||
        state.service_id != HANDOVER_REPLICA_SERVICE_STORAGE ||
        state.epoch != 1U || state.sequence != 1U ||
        !storage_handover_validate(&state) ||
        handover_init_replica(state.source_node, 2U, lease_ms, state.epoch,
                              1U, now) != 0 ||
        handover_replica_init(&state) != 0)
        panic("Unable to receive active handover state");
    for (uint64_t expected = 2U; expected <= 3U; ++expected) {
        if (!handover_serial_receive_state(&state) ||
            state.sequence != expected || !storage_handover_validate(&state) ||
            handover_replica_apply(&state) != 0)
            panic("Unable to apply sequenced service state");
    }
    handover_status_t replicated;
    if (handover_snapshot(&replicated) != 0 || replicated.active_node != 1U ||
        replicated.standby_node != 2U || replicated.epoch != 1U ||
        handover_replica_snapshot(&state) != 0 || state.sequence != 3U ||
        !storage_handover_validate(&state))
        panic("Replicated handover state validation failed");
    printf("REIST_HANDOVER STANDBY_STATE_APPLIED\n");
#elif REIST_HANDOVER_NODE_ID == 3
    if (!handover_serial_send_ready(1U, 2U))
        panic("Unable to announce repaired channel readiness");
    handover_replica_state_t state;
    if (!handover_serial_receive_state(&state) || state.source_node != 2U ||
        state.service_id != HANDOVER_REPLICA_SERVICE_STORAGE ||
        state.epoch != 2U || state.sequence != 4U ||
        !storage_handover_validate(&state) ||
        handover_init_replica(2U, 1U, lease_ms, 2U, 3U, now) != 0 ||
        handover_replica_init(&state) != 0)
        panic("Unable to reintegrate repaired standby state");
    if (handover_renew(1U, 2U, now) >= 0 ||
        handover_takeover(1U, 2U, now) >= 0)
        panic("Reintegrated standby acquired authority");
    printf("REIST_HANDOVER REJOIN_STATE_APPLIED\n");
    if (!storage_handover_is_held())
        panic("Repaired channel storage outputs were released");
    printf("REIST_HANDOVER REJOIN_STORAGE_HELD\n");
    printf("REIST_HANDOVER REJOIN_FENCED\n");
    return;
#else
    if (handover_init(1U, 2U, lease_ms, now) != 0)
        panic("Unable to initialize standalone handover state");
#endif

    pit_delay(lease_ms + 10U);
    now = pit_monotonic_ms();
    if (handover_request_fence(1U, now) != 0)
        panic("External handover fence request failed");
    printf("REIST_HANDOVER REQUEST_SENT\n");
    if (handover_confirm_fenced(1U, now) != 0)
        panic("External handover fence readback failed");
    printf("REIST_HANDOVER FENCE_CONFIRMED\n");
    if (handover_takeover(2U, 1U, now) != 0)
        panic("External handover takeover failed");
    handover_status_t status;
    if (handover_snapshot(&status) != 0 || status.active_node != 2U ||
        status.standby_node != 1U || status.epoch != 2U)
        panic("External handover state validation failed");
    printf("REIST_HANDOVER TAKEOVER_OK\n");
#if REIST_HANDOVER_NODE_ID == 2
    if (handover_replica_snapshot(&state) != 0 ||
        handover_replica_promote(2U, 2U, state.value) != 0 ||
        handover_replica_snapshot(&state) != 0 ||
        !storage_handover_validate(&state) ||
        !handover_serial_send_state(&state) ||
        !storage_handover_release(&state) || storage_handover_is_held())
        panic("Unable to publish promoted service state");
    printf("REIST_HANDOVER STORAGE_OUTPUT_RELEASED\n");
    printf("REIST_HANDOVER TAKEOVER_STATE_SENT\n");
#endif
}
#endif

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
    (void)boot_info;
    for (int index = 0; index < drive_count; ++index) {
        drive_t *drive = &detected_drives[index];
        if (strcmp(drive->mount_point, "/") != 0) continue;

        char program_path[PROCESS_PATH_MAX];
        if (!program_path_for_drive(drive, filename, program_path)) break;
        const char *arguments[] = {filename};
        /* System programs are loaded only from the already selected root
         * volume.  An auxiliary disk must not override the trusted shell by
         * appearing earlier in controller discovery order.  Program loading
         * performs sleepable VFS I/O; process-slot and scheduler publication
         * are synchronized inside the process loader. */
        int pid = create_process_for_file_args(
            program_path, 1, arguments, drive->mount_point);
        if (pid >= 0) {
            printf("Starting %s from %s\n", description, program_path);
            wait_for_process(pid);
            printf("%s exited.\n", description);
            return 0;
        }
        break;
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

    /* Per-CPU identity must exist before init_paging binds CR3 to the BSP. */
    if (!x86_cpu_local_bootstrap(x86_cpu_initial_apic_id())) {
        printf("Fatal: unable to initialize BSP per-CPU state.\n");
        while (1) { asm volatile("hlt"); }
    }

    // Parse bootloader-provided information
    parse_multiboot1_info(multiboot_info);
    /* Page zero is deliberately unmapped by paging. Capture the legacy BDA
     * EBDA pointer while the boot identity mapping still permits access. */
    x86_acpi_capture_early();
    if (!boot_health_capture()) {
        panic("Invalid BIOS boot-health handoff");
    }

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
    if (memory_reserve_region(X86_SMP_TRAMPOLINE_BASE,
                              X86_SMP_TRAMPOLINE_REGION_SIZE) != 0) {
        panic("Unable to reserve SMP trampoline region");
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
    boot_context("kernel-init", "fatal recovery", "recover",
                 "persistent crash record");
    fatal_boot_recover_record();
    boot_context("kernel-init", "supervisor", "initialize", "core domains");
    supervisor_init();
    boot_context("kernel-init", "storage maintenance", "initialize",
                 "maintenance leases");
    if (!storage_maintenance_init()) {
        panic_context_set_result(-1, 0U, 0U);
        printf("Kernel: storage maintenance initialization failed.\n");
        panic("Unable to initialize storage maintenance leases");
    }
    boot_context("kernel-init", "storage service", "initialize",
                 "service control");
    if (!storage_service_init()) {
        panic_context_set_result(-1, 0U, 0U);
        panic("Unable to initialize REIST storage service control");
    }
    boot_context("kernel-init", "network", "initialize", "protected state");
    if (!netstack_safety_init()) {
        panic_context_set_result(-1, 0U, 0U);
        panic("Unable to initialize protected network state");
    }
    boot_context("kernel-init", "IPC", "initialize", "endpoint tables");
    ipc_init();
    boot_context("kernel-init", "output fence", "initialize", "fence registry");
    output_fence_init();
    boot_context("kernel-init", "filesystem safety", "initialize",
                 "mutation supervision");
    if (!filesystem_safety_init(pit_monotonic_ms())) {
        panic_context_set_result(-1, 0U, 0U);
        panic("Unable to initialize REIST filesystem supervision");
    }
    if (!output_fence_register(filesystem_fence_mutations)) {
        panic("Unable to register the filesystem mutation fence");
    }
    if (!output_fence_register(netdev_fence_outputs)) {
        panic("Unable to register the network output fence");
    }
    
    // Stage 2: Hardware initialization
    hardware_init();
    
    // Stage 3: Driver initialization
    driver_init(multiboot_info);
    
    network_initialize();
    watchdog_init();
    printf("Watchdog: %s\n", watchdog_available() ? "IB700 armed" : "external backend required");
#ifdef REIST_HANDOVER_FAULT_INJECTION
    test_external_handover_channel();
#endif
#ifdef REIST_FAULT_INJECTION
    if (fatal_last_crash_record()->magic != FATAL_CRASH_RECORD_MAGIC) {
        printf("REIST_TEST DOUBLE_FAULT_ARMED\n");
        fatal_test_trigger_double_fault();
    }
    printf("REIST_TEST FATAL_RECOVERY_OK\n");
#endif
    boot_context("userspace-start", "REIST probe", "spawn",
                 "/libexec/reist/reist.prg");
    if (!supervisor_start_probe(pit_monotonic_ms())) {
        panic("Unable to start REIST Ring-3 probe");
    }
    boot_context("userspace-start", "storage service", "spawn",
                 "/libexec/reist/storage.prg");
    if (!storage_service_start(pit_monotonic_ms())) {
        panic("Unable to start REIST Ring-3 storage service");
    }
    supervisor_handle_t video_driver_handle = {0U, 0U, 0U};
    bool video_driver_started = false;
    uint32_t video_ap_mask = 0U;
    supervisor_handle_t audio_driver_handle = {0U, 0U, 0U};
    bool audio_driver_started = false;
    bool audio_service_started = false;
    x86_smp_status_t production_driver_smp_status;
    x86_smp_status(&production_driver_smp_status);
    uint32_t production_driver_ap_mask =
        production_driver_smp_status.online_cpu_count > 1U
            ? ((1U << production_driver_smp_status.online_cpu_count) - 1U) &
                ~1U
            : 0U;
    uint32_t audio_ap_mask = production_driver_ap_mask;
    if (video_device_available) {
        video_ap_mask = production_driver_ap_mask;
        /* GK208 executes several independently bounded GR construction
         * phases before it may publish output authority. Each phase must
         * advance within six seconds and the complete generation remains
         * capped at one minute. Heartbeat and fencing stay fast. */
        const uint32_t video_startup_timeout_ms =
            video_device_info.backend == VIDEO_DEVICE_BACKEND_NVIDIA_GK208
                ? 60000U : 1000U;
        const uint32_t video_startup_progress_timeout_ms =
            video_device_info.backend == VIDEO_DEVICE_BACKEND_NVIDIA_GK208
                ? 6000U : 0U;
        const supervisor_config_t video_driver_config = {
            .heartbeat_timeout_ms = 2000U,
            .recovery_timeout_ms = 1000U,
            .restart_budget = 3U,
            .startup_timeout_ms = video_startup_timeout_ms,
            .startup_progress_timeout_ms = video_startup_progress_timeout_ms,
        };
        const bool nvidia = video_device_info.backend ==
            VIDEO_DEVICE_BACKEND_NVIDIA_GK208;
        const char *driver_name = nvidia
            ? "nvidia-gk208-ring3" : "svga2d-ring3";
        const char *driver_path = nvidia
            ? "/libexec/reist/nvidia.prg" : "/libexec/reist/svga2d.prg";
        boot_context("userspace-start",
                     nvidia ? "NVIDIA GK208 driver" : "SVGA-II driver",
                     "spawn", driver_path);
        int video_driver_result = supervisor_start_device_driver(
            driver_name, driver_path,
            video_device_info.device_index, DEVICE_DOMAIN_MODE_MEDIATED,
            &video_driver_config, pit_monotonic_ms(), &video_driver_handle);
        if (video_driver_result != 0)
            printf("REIST_VIDEO DRIVER_DEGRADED result=%d\n",
                   video_driver_result);
        else
            video_driver_started = true;
    }
    if (audio_device_available) {
        const supervisor_config_t audio_driver_config = {
            /* HDA reports every 500 ms. Allow bounded SMP scheduling and
             * legacy-PIC delivery jitter without weakening the one-second
             * fence or the finite restart budget. */
            .heartbeat_timeout_ms = 5000U,
            .recovery_timeout_ms = 1000U,
            .restart_budget = 3U,
        };
        boot_context("userspace-start", "HDA driver", "spawn",
                     "/libexec/reist/hda.prg");
        int audio_driver_result = supervisor_start_device_driver(
            "hda-ring3", "/libexec/reist/hda.prg",
            audio_device_info.device_index, DEVICE_DOMAIN_MODE_MEDIATED,
            &audio_driver_config, pit_monotonic_ms(), &audio_driver_handle);
        if (audio_driver_result != 0) {
            printf("REIST_AUDIO DRIVER_DEGRADED result=%d\n",
                   audio_driver_result);
        } else {
            audio_driver_started = true;
            boot_context("userspace-start", "audio service", "spawn",
                         "/libexec/reist/audio.prg");
            if (!supervisor_start_audio_service(
                    audio_device_info.device_index, pit_monotonic_ms()))
                printf("REIST_AUDIO SERVICE_DEGRADED result=-1\n");
            else
                audio_service_started = true;
        }
    }
    /* Publish every supervised service before the worker can inspect or
     * restart it.  This removes a boot-time partial-initialization race. */
    boot_context("userspace-start", "safety supervisor", "spawn",
                 "kernel worker");
    if (!supervisor_start_worker()) {
        panic_context_set_result(-1, 0U, 0U);
        panic("Unable to start REIST safety supervisor worker");
    }
    configure_network_after_service();
    // Stage 4: System ready
    system_ready();
    boot_context("smp-release", "storage", "parallel-read-probe",
                 "active root block device");
    if (!smp_storage_probe_prepare()) {
        panic("Unable to prepare SMP storage serialization probe");
    }
    printf("BOOT_OK\n");
    boot_health_mark_system_ready();
    boot_context("smp-release", "scheduler", "probe",
                 "application processors");
    if (!x86_smp_scheduler_probe()) {
        panic("SMP scheduler release probe failed");
    }
    if (production_driver_ap_mask != 0U &&
        storage_service_set_current_affinity(production_driver_ap_mask) != 0)
        panic("Unable to move healthy storage service generation to APs");
    if (production_driver_ap_mask != 0U &&
        supervisor_set_network_service_current_affinity(
            production_driver_ap_mask) != 0)
        panic("Unable to move healthy network service generation to APs");
    if (video_driver_started && video_ap_mask != 0U &&
        video_device_info.backend == VIDEO_DEVICE_BACKEND_VMWARE_SVGA2) {
        if (supervisor_set_device_driver_current_affinity(
                video_driver_handle, video_ap_mask) != 0)
            panic("Unable to move healthy SVGA2D generation to APs");
    }
    if (audio_driver_started && audio_ap_mask != 0U) {
        if (supervisor_set_device_driver_current_affinity(
                audio_driver_handle, audio_ap_mask) != 0)
            panic("Unable to move healthy HDA generation to APs");
    }
    if (audio_service_started && audio_ap_mask != 0U) {
        if (supervisor_set_audio_service_current_affinity(audio_ap_mask) != 0)
            panic("Unable to move healthy audio service generation to APs");
    }
#ifdef REIST_DRIVER_DOMAIN_FAULT_INJECTION
    /* Fault fixtures are intentionally registered only after AP scheduling is
     * live. Production services retain the pre-worker publication order. */
    x86_smp_status_t driver_fault_smp_status;
    x86_smp_status(&driver_fault_smp_status);
    uint32_t driver_fault_ap_mask = driver_fault_smp_status.online_cpu_count > 1U
        ? ((1U << driver_fault_smp_status.online_cpu_count) - 1U) & ~1U : 0U;
    if (driver_fault_ap_mask == 0U)
        panic("Driver-domain SMP fault fixture requires an online AP");
    const supervisor_config_t driver_fault_config = {
        .heartbeat_timeout_ms = 150U,
        .recovery_timeout_ms = 750U,
        .restart_budget = 3U,
        .cpu_affinity_mask = driver_fault_ap_mask,
    };
    supervisor_handle_t driver_fault_handle;
    if (supervisor_start_device_driver(
            "driver-fault-recovery", "/libexec/reist/reist.prg",
            driver_fault_recovery_device, DEVICE_DOMAIN_MODE_MEDIATED,
            &driver_fault_config, pit_monotonic_ms(),
            &driver_fault_handle) != 0)
        panic("Unable to start recoverable driver-domain fixture");
    const supervisor_config_t reset_fault_config = {
        .heartbeat_timeout_ms = 150U,
        .recovery_timeout_ms = 750U,
        .restart_budget = 1U,
    };
    supervisor_handle_t reset_fault_handle;
    if (supervisor_start_device_driver(
            "driver-fault-reset", "/libexec/reist/reist.prg",
            driver_fault_reset_device, DEVICE_DOMAIN_MODE_MEDIATED,
            &reset_fault_config, pit_monotonic_ms(),
            &reset_fault_handle) != 0)
        panic("Unable to start reset-failure driver-domain fixture");
    printf("DRIVER_DOMAIN TEST_STARTED\n");
#endif
    /* A real framebuffer prefers the graphical desktop.  VGA boots and any
     * failed/terminated desktop fall back to the userspace shell. */
    if (!supervisor_start_compositor(pit_monotonic_ms(),
                                     production_driver_ap_mask)) {
        printf("Unable to start desktop.prg; starting shell fallback.\n");
    } else {
        printf("Starting supervised graphical desktop from "
               "/usr/gui/bin/desktop.prg\n");
        while (supervisor_compositor_session_active()) {
            if (scheduler_sleep_ms(10U) != 0) (void)scheduler_yield();
        }
        printf("Graphical desktop lifecycle ended; starting shell "
               "fallback.\n");
    }
    if (start_userspace_program(multiboot_info, "bin/shell.prg",
                                "userspace command interpreter") < 0) {
        printf("Unable to start shell.prg; entering rescue shell.\n");
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
