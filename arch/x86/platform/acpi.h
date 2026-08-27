/**
 * @file arch/x86/platform/acpi.h
 * @brief Bounded ACPI inventory used by generic x86 resource mediation.
 *
 * Discovery is diagnostic only. A valid DMAR table proves neither active
 * translation nor correct isolation groups and therefore grants no DMA right.
 */
#ifndef REIST_X86_PLATFORM_ACPI_H
#define REIST_X86_PLATFORM_ACPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define X86_ACPI_IOMMU_INVENTORY_VERSION 1U
#define X86_ACPI_CPU_INVENTORY_VERSION 1U
#define X86_ACPI_MAX_CPUS 16U

/** Capture the low BIOS Data Area pointer before page zero is unmapped. */
void x86_acpi_capture_early(void);

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t acpi_root_valid;
    uint32_t dmar_present;
    uint32_t dmar_valid;
    uint32_t remapping_unit_count;
    uint32_t interrupt_remapping_reported;
    uint32_t translation_enabled;
    uint32_t direct_assignment_ready;
} x86_acpi_iommu_inventory_t;

_Static_assert(sizeof(x86_acpi_iommu_inventory_t) == 36U,
               "ACPI IOMMU inventory ABI changed");

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t local_apic_address;
    uint32_t discovered_cpu_count;
    uint32_t usable_cpu_count;
    uint32_t truncated_cpu_count;
    uint32_t unsupported_x2apic_count;
    uint32_t duplicate_apic_id_count;
    uint32_t apic_ids[X86_ACPI_MAX_CPUS];
} x86_acpi_cpu_inventory_t;

_Static_assert(sizeof(x86_acpi_cpu_inventory_t) == 96U,
               "ACPI CPU inventory ABI changed");

/** Scan the bounded BIOS ACPI locations and inventory an optional DMAR table. */
bool x86_acpi_iommu_inventory(x86_acpi_iommu_inventory_t *inventory);

/** Parse one already bounded DMAR table; exported for deterministic host tests. */
bool x86_acpi_parse_dmar(const void *table, size_t available_length,
                         x86_acpi_iommu_inventory_t *inventory);

/** Parse a bounded ACPI MADT and retain enabled xAPIC CPU identifiers. */
bool x86_acpi_parse_madt(const void *table, size_t available_length,
                         x86_acpi_cpu_inventory_t *inventory);

/** Discover and parse the platform MADT through the validated ACPI root. */
bool x86_acpi_cpu_inventory(x86_acpi_cpu_inventory_t *inventory);

#endif
