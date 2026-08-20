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

/** Scan the bounded BIOS ACPI locations and inventory an optional DMAR table. */
bool x86_acpi_iommu_inventory(x86_acpi_iommu_inventory_t *inventory);

/** Parse one already bounded DMAR table; exported for deterministic host tests. */
bool x86_acpi_parse_dmar(const void *table, size_t available_length,
                         x86_acpi_iommu_inventory_t *inventory);

#endif
