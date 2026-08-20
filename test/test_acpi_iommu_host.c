#include "arch/x86/platform/acpi.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void seal_checksum(uint8_t *table, size_t length) {
    table[9U] = 0U;
    uint8_t sum = 0U;
    for (size_t index = 0U; index < length; ++index) sum += table[index];
    table[9U] = (uint8_t)(0U - sum);
}

int main(void) {
    uint8_t table[64U];
    memset(table, 0, sizeof(table));
    memcpy(table, "DMAR", 4U);
    uint32_t length = sizeof(table);
    memcpy(&table[4U], &length, sizeof(length));
    table[36U] = 39U;
    table[37U] = 1U;
    uint16_t structure_type = 0U;
    uint16_t structure_length = 16U;
    memcpy(&table[48U], &structure_type, sizeof(structure_type));
    memcpy(&table[50U], &structure_length, sizeof(structure_length));
    seal_checksum(table, sizeof(table));

    x86_acpi_iommu_inventory_t inventory = {
        .version = X86_ACPI_IOMMU_INVENTORY_VERSION,
        .struct_size = sizeof(inventory),
        .dmar_present = 1U,
    };
    assert(x86_acpi_parse_dmar(table, sizeof(table), &inventory));
    assert(inventory.dmar_valid == 1U);
    assert(inventory.remapping_unit_count == 1U);
    assert(inventory.interrupt_remapping_reported == 1U);
    assert(inventory.translation_enabled == 0U);
    assert(inventory.direct_assignment_ready == 0U);

    table[12U] ^= 1U;
    assert(!x86_acpi_parse_dmar(table, sizeof(table), &inventory));
    assert(inventory.dmar_valid == 0U);
    table[12U] ^= 1U;
    seal_checksum(table, sizeof(table));
    structure_length = 3U;
    memcpy(&table[50U], &structure_length, sizeof(structure_length));
    seal_checksum(table, sizeof(table));
    assert(!x86_acpi_parse_dmar(table, sizeof(table), &inventory));
    return 0;
}
