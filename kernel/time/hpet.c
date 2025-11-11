#include "kernel/time/hpet.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"
#include <stdint.h>
#include <stddef.h>
#include "arch/x86/include/sys.h"

#define HPET_BASE_ADDRESS  0xFED00000  // Example; get this from ACPI HPET table
#define HPET_REG_SIZE      0x400       // HPET register space size

volatile uint64_t* hpet_base = (volatile uint64_t*)HPET_BASE_ADDRESS;

// Access HPET registers using offsets
#define HPET_CAPABILITIES      0x00 // General Capabilities Register
#define HPET_CONFIGURATION     0x10 // General Configuration Register
#define HPET_MAIN_COUNTER      0xF0 // Main Counter Register
#define HPET_TIMER_CONFIG(n)   (0x100 + (n * 0x20)) // Timer N Configuration
#define HPET_TIMER_COMPARATOR(n) (0x108 + (n * 0x20)) // Timer N Comparator

#define RSDP_SIGNATURE "RSD PTR "
#define HPET_SIGNATURE "HPET"

typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address; // Physical address of RSDT
} __attribute__((packed)) RSDPDescriptor;

typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_table_header;

typedef struct {
    acpi_table_header header;
    uint32_t event_timer_block_id;
    uint8_t base_address[12]; // ACPI Generic Address Structure
    uint8_t hpet_number;
    uint16_t minimum_tick;
    uint8_t attributes; // Bit 0: LegacyReplacementRouteCapable
} __attribute__((packed)) HPETTable;

RSDPDescriptor* find_rsdp();


#define APIC_SIGNATURE "APIC"

typedef struct {
    acpi_table_header header;
    uint32_t lapic_address;
    uint32_t flags;
    uint8_t entries[];
} __attribute__((packed)) MADT;

typedef struct {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) madt_entry_header;

typedef struct {
    madt_entry_header header;
    uint8_t source_irq;
    uint32_t global_system_interrupt;
    uint16_t flags;
} __attribute__((packed)) madt_interrupt_source_override;

void* map_physical_memory(uintptr_t physical_address, size_t size) {
    // In systems without paging, physical memory can be directly accessed
    return (void*)physical_address;
}

int get_hpet_irq_from_madt(uint8_t* irq) {
    RSDPDescriptor* rsdp = find_rsdp();
    if (!rsdp) {
        printf("RSDP not found\n");
        return -1;
    }

    acpi_table_header* rsdt = (acpi_table_header*)map_physical_memory(rsdp->rsdt_address, sizeof(acpi_table_header));
    uint32_t* entries = (uint32_t*)((uintptr_t)rsdt + sizeof(acpi_table_header));
    int entry_count = (rsdt->length - sizeof(acpi_table_header)) / sizeof(uint32_t);

    for (int i = 0; i < entry_count; i++) {
        acpi_table_header* header = (acpi_table_header*)map_physical_memory(entries[i], sizeof(acpi_table_header));
        if (memcmp(header->signature, APIC_SIGNATURE, 4) == 0) {
            MADT* madt = (MADT*)header;

            uint8_t* ptr = madt->entries;
            uint8_t* end = (uint8_t*)madt + madt->header.length;

            while (ptr < end) {
                madt_entry_header* entry = (madt_entry_header*)ptr;

                if (entry->type == 2) { // Type 2: Interrupt Source Override
                    madt_interrupt_source_override* iso = (madt_interrupt_source_override*)entry;
                    if (iso->source_irq == 0) { // Check if it overrides IRQ 0
                        *irq = iso->global_system_interrupt;
                        printf("HPET IRQ from MADT: %u\n", *irq);
                        return 0;
                    }
                }

                ptr += entry->length;
            }
        }
    }

    printf("HPET IRQ not found in MADT\n");
    return -1;
}

// Locate the RSDP
RSDPDescriptor* find_rsdp() {
    for (uint8_t* addr = (uint8_t*)0x000E0000; addr < (uint8_t*)0x00100000; addr += 16) {
        if (memcmp(addr, RSDP_SIGNATURE, 8) == 0) {
            return (RSDPDescriptor*)addr;
        }
    }
    return NULL;
}

volatile uint64_t hpet_interrupt_count = 0;

void hpet_timer_isr(void* r) {
    hpet_interrupt_count++;

    //Acknowledge the interrupt by updating the comparator
    uint64_t current_counter = hpet_base[HPET_MAIN_COUNTER / 8];
    uint64_t ticks = hpet_base[HPET_TIMER_COMPARATOR(0) / 8];
    hpet_base[HPET_TIMER_COMPARATOR(0) / 8] = current_counter + ticks;

    printf("Periodic timer callback triggered: %u\n", hpet_interrupt_count);
}

// Check for HPET
int check_hpet() {
    RSDPDescriptor* rsdp = find_rsdp();
    if (!rsdp) return 0;

    acpi_table_header* rsdt = (acpi_table_header*)(uintptr_t)rsdp->rsdt_address;
    uint32_t* entries = (uint32_t*)((uintptr_t)rsdt + sizeof(acpi_table_header));
    int entry_count = (rsdt->length - sizeof(acpi_table_header)) / sizeof(uint32_t);

    for (int i = 0; i < entry_count; i++) {
        acpi_table_header* header = (acpi_table_header*)(uintptr_t)entries[i];
        if (memcmp(header->signature, HPET_SIGNATURE, 4) == 0) {
            return 1; // HPET is supported
        }
    }

    return 0; // HPET not found
}

void enable_hpet() {
    uint64_t config = hpet_base[HPET_CONFIGURATION / 8];

    // Enable HPET globally (bit 0) and start the main counter
    config |= 0x1;   // Enable HPET
    config &= ~(1 << 2); // Clear the LegacyReplacementRouteCapable bit

    hpet_base[HPET_CONFIGURATION / 8] = config;
}

uint64_t get_hpet_frequency() {
    uint64_t capabilities = hpet_base[HPET_CAPABILITIES / 8];
    uint64_t period_fs = capabilities >> 32; // Extract the period in femtoseconds
    if (period_fs == 0) {
        printf("HPET period is zero, frequency cannot be determined\n");
        return 0;
    }

    printf("HPET period (fs): %u\n", period_fs);

    return 1000000000000000ULL / period_fs; // Convert to frequency in Hz
}

void set_hpet_timer(uint8_t timer, uint64_t interval_ns, int periodic) {
    uint64_t frequency = get_hpet_frequency();
    uint64_t ticks = (frequency * interval_ns) / 1000000000ULL;

    // Disable the timer while configuring it
    hpet_base[(HPET_TIMER_CONFIG(timer)) / 8] &= ~0x1;

    // Set comparator value
    uint64_t current_counter = hpet_base[HPET_MAIN_COUNTER / 8];
    hpet_base[(HPET_TIMER_COMPARATOR(timer)) / 8] = current_counter + ticks;

    // Configure periodic mode if needed
    if (periodic) {
        hpet_base[(HPET_TIMER_CONFIG(timer)) / 8] |= (1 << 3); // Set periodic bit
    }

    // Enable the timer and interrupts
    hpet_base[(HPET_TIMER_CONFIG(timer)) / 8] |= 0x1;
}

void* get_hpet_base_from_acpi() {
    RSDPDescriptor* rsdp = find_rsdp();
    if (!rsdp) {
        printf("RSDP not found\n");
        return NULL;
    }

    acpi_table_header* rsdt = (acpi_table_header*)(uintptr_t)rsdp->rsdt_address;
    if (memcmp(rsdt->signature, "RSDT", 4) != 0) {
        printf("RSDT not found\n");
        return NULL;
    }

    uint32_t* entries = (uint32_t*)((uintptr_t)rsdt + sizeof(acpi_table_header));
    int entry_count = (rsdt->length - sizeof(acpi_table_header)) / sizeof(uint32_t);

    for (int i = 0; i < entry_count; i++) {
        acpi_table_header* header = (acpi_table_header*)(uintptr_t)entries[i];
        if (memcmp(header->signature, "HPET", 4) == 0) {
            // HPET table found
            uint8_t* hpet_table = (uint8_t*)header;

            // Extract the base address
            uint64_t base_address = *(uint64_t*)(hpet_table + 0x2C); // Offset 0x2C in HPET table
            printf("HPET base address extracted: %p\n", base_address);

            return (void*)(uintptr_t)base_address;
        }
    }

    printf("HPET table not found in ACPI\n");
    return NULL;
}

void initialize_hpet() {
    // Validate and set HPET base address from ACPI
    void* base = get_hpet_base_from_acpi();
    if (!base) {
        printf("HPET base address not found\n");
        return;
    }

    hpet_base = (volatile uint64_t*)base;

    printf("HPET base address: %p\n", hpet_base);

    // Enable HPET globally and main counter
    enable_hpet();
}

void test_hpet_main_counter() {
    printf("Testing HPET main counter...\n");

    uint64_t capabilities = hpet_base[HPET_CAPABILITIES / 8];
    uint64_t period_fs = capabilities >> 32; // HPET period in femtoseconds
    uint64_t frequency = 1000000000000000ULL / period_fs; // Convert to Hz

    printf("HPET period (fs): %llu, Frequency: %llu Hz\n", period_fs, frequency);

    uint64_t counter_start = hpet_base[HPET_MAIN_COUNTER / 8];
    for (volatile int i = 0; i < 100000000; i++); // Delay loop
    uint64_t counter_end = hpet_base[HPET_MAIN_COUNTER / 8];

    uint64_t elapsed_ticks = counter_end - counter_start;
    uint64_t elapsed_ns = elapsed_ticks * (period_fs / 1000); // Calculate elapsed time

    printf("HPET test: Elapsed ticks = %llu, Elapsed time = %llu ns\n", (unsigned long long)elapsed_ticks, (unsigned long long)elapsed_ns);
}

void hpet_set_periodic_timer(uint8_t timer, uint64_t interval_ns) {
    uint64_t frequency = 1000000000000000ULL / (hpet_base[HPET_CAPABILITIES / 8] >> 32); // HPET frequency in Hz
    uint64_t ticks = (frequency * interval_ns) / 1000000000ULL; // Convert ns to ticks

    // Disable the timer while configuring it
    hpet_base[HPET_TIMER_CONFIG(timer) / 8] &= ~0x1;

    // Set comparator value
    hpet_base[HPET_TIMER_COMPARATOR(timer) / 8] = ticks;

    // Configure periodic mode
    hpet_base[HPET_TIMER_CONFIG(timer) / 8] |= (1 << 3); // Periodic mode

    // Enable the timer and interrupts
    hpet_base[HPET_TIMER_CONFIG(timer) / 8] |= 0x1;

    printf("HPET periodic timer configured: interval = %llu ns, ticks = %llu\n", interval_ns, ticks);
}

void initialize_hpet_periodic_callback(uint64_t interval_ns) {
    printf("Initializing HPET periodic timer...\n");

    // Enable HPET
    uint64_t config = hpet_base[HPET_CONFIGURATION / 8];
    hpet_base[HPET_CONFIGURATION / 8] = config | 0x1;

    // Configure periodic timer (e.g., Timer 0)
    hpet_set_periodic_timer(0, interval_ns);
}

void hpet_init() {
    if (check_hpet()) {
        printf("HPET is supported\n");

        // Initialize HPET
        initialize_hpet();

         // Setup HPET interrupt
        register_interrupt_handler(2, (void*)hpet_timer_isr);
    } else {
        printf("HPET is not supported\n");
    }
}
