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

static size_t add_local_apic(uint8_t *table, size_t offset,
                             uint8_t processor_id, uint8_t apic_id,
                             uint32_t flags) {
    table[offset] = 0U;
    table[offset + 1U] = 8U;
    table[offset + 2U] = processor_id;
    table[offset + 3U] = apic_id;
    memcpy(table + offset + 4U, &flags, sizeof(flags));
    return offset + 8U;
}

int main(void) {
    uint8_t table[96U];
    memset(table, 0, sizeof(table));
    memcpy(table, "APIC", 4U);
    uint32_t lapic_address = 0xFEE00000U;
    memcpy(table + 36U, &lapic_address, sizeof(lapic_address));
    size_t length = add_local_apic(table, 44U, 0U, 0U, 1U);
    length = add_local_apic(table, length, 1U, 1U, 1U);
    length = add_local_apic(table, length, 2U, 2U, 0U);
    uint32_t length32 = (uint32_t)length;
    memcpy(table + 4U, &length32, sizeof(length32));
    seal_checksum(table, length);

    x86_acpi_cpu_inventory_t inventory;
    assert(x86_acpi_parse_madt(table, length, &inventory));
    assert(inventory.local_apic_address == 0xFEE00000U);
    assert(inventory.discovered_cpu_count == 2U);
    assert(inventory.usable_cpu_count == 2U);
    assert(inventory.apic_ids[0] == 0U);
    assert(inventory.apic_ids[1] == 1U);

    table[12U] ^= 1U;
    assert(!x86_acpi_parse_madt(table, length, &inventory));
    table[12U] ^= 1U;
    table[45U] = 1U;
    seal_checksum(table, length);
    assert(!x86_acpi_parse_madt(table, length, &inventory));
    return 0;
}
