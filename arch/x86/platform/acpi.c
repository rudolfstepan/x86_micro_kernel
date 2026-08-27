/**
 * @file arch/x86/platform/acpi.c
 * @brief Fail-closed ACPI root and Intel DMAR inventory for 32-bit x86.
 */
#include "arch/x86/platform/acpi.h"

#include "arch/x86/mm/paging.h"
#ifdef REIST_HOST_TEST
#include <string.h>
#else
#include "lib/libc/string.h"
#endif

#define ACPI_RSDP_V1_LENGTH 20U
#define ACPI_RSDP_V2_LENGTH 36U
#define ACPI_RSDP_MAX_LENGTH 64U
#define ACPI_SDT_HEADER_LENGTH 36U
#define ACPI_TABLE_MAX_LENGTH (1024U * 1024U)
#define ACPI_MAX_ROOT_ENTRIES 256U
#define ACPI_MAX_DMAR_STRUCTURES 256U
#define ACPI_MAX_MADT_STRUCTURES 256U
#define ACPI_EBDA_SCAN_LENGTH 1024U

#pragma pack(push, 1)
typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} acpi_rsdp_t;

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
} acpi_sdt_header_t;

typedef struct {
    acpi_sdt_header_t header;
    uint8_t host_address_width;
    uint8_t flags;
    uint8_t reserved[10];
} acpi_dmar_t;

typedef struct {
    acpi_sdt_header_t header;
    uint32_t local_apic_address;
    uint32_t flags;
} acpi_madt_t;

typedef struct {
    uint16_t type;
    uint16_t length;
} acpi_dmar_structure_t;
#pragma pack(pop)

_Static_assert(sizeof(acpi_rsdp_t) == ACPI_RSDP_V2_LENGTH,
               "RSDP layout mismatch");
_Static_assert(sizeof(acpi_sdt_header_t) == ACPI_SDT_HEADER_LENGTH,
               "ACPI SDT header layout mismatch");
_Static_assert(sizeof(acpi_dmar_t) == 48U, "DMAR header layout mismatch");
_Static_assert(sizeof(acpi_madt_t) == 44U, "MADT header layout mismatch");

static uint32_t captured_ebda;

void x86_acpi_capture_early(void) {
#ifndef REIST_HOST_TEST
    uint16_t ebda_segment = 0U;
    __asm__ volatile("movw 0x40e, %0" : "=r"(ebda_segment));
    uint32_t candidate = (uint32_t)ebda_segment << 4U;
    captured_ebda = candidate >= 0x400U && candidate < 0xA0000U
        ? candidate : 0U;
#else
    captured_ebda = 0U;
#endif
}

static bool checksum_valid(const void *data, size_t length) {
    if (data == NULL || length == 0U) return false;
    const uint8_t *bytes = data;
    uint8_t sum = 0U;
    for (size_t index = 0U; index < length; ++index) sum += bytes[index];
    return sum == 0U;
}

static bool physical_range_valid(uint64_t address, size_t length) {
    if (address == 0U || address > UINT32_MAX || length == 0U ||
        length > ACPI_TABLE_MAX_LENGTH) return false;
    uint64_t end = address + (uint64_t)length;
    return end > address && end <= KERNEL_IDENTITY_LIMIT;
}

static const acpi_rsdp_t *rsdp_at(uint32_t address) {
    if (!physical_range_valid(address, ACPI_RSDP_V1_LENGTH)) return NULL;
    const acpi_rsdp_t *rsdp = (const acpi_rsdp_t *)(uintptr_t)address;
    if (memcmp(rsdp->signature, "RSD PTR ", 8U) != 0 ||
        !checksum_valid(rsdp, ACPI_RSDP_V1_LENGTH)) return NULL;
    if (rsdp->revision < 2U) return rsdp;
    if (rsdp->length < ACPI_RSDP_V2_LENGTH ||
        rsdp->length > ACPI_RSDP_MAX_LENGTH ||
        !physical_range_valid(address, rsdp->length) ||
        !checksum_valid(rsdp, rsdp->length)) return NULL;
    return rsdp;
}

static const acpi_rsdp_t *scan_rsdp_range(uint32_t start, uint32_t length) {
    if ((start & 0x0FU) != 0U || length < ACPI_RSDP_V1_LENGTH ||
        !physical_range_valid(start, length)) return NULL;
    uint32_t last = start + length - ACPI_RSDP_V1_LENGTH;
    for (uint32_t address = start; address <= last; address += 16U) {
        const acpi_rsdp_t *rsdp = rsdp_at(address);
        if (rsdp != NULL) return rsdp;
    }
    return NULL;
}

static const acpi_rsdp_t *find_rsdp(void) {
    const acpi_rsdp_t *rsdp = scan_rsdp_range(0xE0000U, 0x20000U);
    if (rsdp == NULL && captured_ebda != 0U)
        rsdp = scan_rsdp_range(captured_ebda, ACPI_EBDA_SCAN_LENGTH);
    return rsdp;
}

static const acpi_sdt_header_t *validated_sdt(uint64_t address,
                                              const char signature[4]) {
    if (!physical_range_valid(address, sizeof(acpi_sdt_header_t))) return NULL;
    const acpi_sdt_header_t *header =
        (const acpi_sdt_header_t *)(uintptr_t)(uint32_t)address;
    if (memcmp(header->signature, signature, 4U) != 0 ||
        header->length < sizeof(*header) ||
        header->length > ACPI_TABLE_MAX_LENGTH ||
        !physical_range_valid(address, header->length) ||
        !checksum_valid(header, header->length)) return NULL;
    return header;
}

static const acpi_sdt_header_t *find_root(const acpi_rsdp_t *rsdp,
                                          uint32_t *entry_width) {
    if (rsdp == NULL || entry_width == NULL) return NULL;
    if (rsdp->revision >= 2U && rsdp->xsdt_address != 0U) {
        const acpi_sdt_header_t *xsdt =
            validated_sdt(rsdp->xsdt_address, "XSDT");
        if (xsdt != NULL) {
            *entry_width = 8U;
            return xsdt;
        }
    }
    const acpi_sdt_header_t *rsdt =
        validated_sdt(rsdp->rsdt_address, "RSDT");
    if (rsdt != NULL) *entry_width = 4U;
    return rsdt;
}

static const acpi_sdt_header_t *find_sdt(const char signature[4]) {
    const acpi_rsdp_t *rsdp = find_rsdp();
    uint32_t entry_width = 0U;
    const acpi_sdt_header_t *root = find_root(rsdp, &entry_width);
    if (root == NULL || entry_width == 0U) return NULL;
    size_t payload_length = root->length - sizeof(*root);
    if (payload_length % entry_width != 0U ||
        payload_length / entry_width > ACPI_MAX_ROOT_ENTRIES) return NULL;
    const uint8_t *entries = (const uint8_t *)root + sizeof(*root);
    uint32_t count = (uint32_t)(payload_length / entry_width);
    for (uint32_t index = 0U; index < count; ++index) {
        uint64_t address = 0U;
        memcpy(&address, entries + (size_t)index * entry_width, entry_width);
        const acpi_sdt_header_t *header = validated_sdt(address, signature);
        if (header != NULL) return header;
    }
    return NULL;
}

static void cpu_inventory_add(x86_acpi_cpu_inventory_t *inventory,
                              uint32_t apic_id) {
    if (inventory->discovered_cpu_count != UINT32_MAX)
        ++inventory->discovered_cpu_count;
    if (apic_id > UINT8_MAX) {
        if (inventory->unsupported_x2apic_count != UINT32_MAX)
            ++inventory->unsupported_x2apic_count;
        return;
    }
    for (uint32_t index = 0U; index < inventory->usable_cpu_count; ++index) {
        if (inventory->apic_ids[index] != apic_id) continue;
        if (inventory->duplicate_apic_id_count != UINT32_MAX)
            ++inventory->duplicate_apic_id_count;
        return;
    }
    if (inventory->usable_cpu_count >= X86_ACPI_MAX_CPUS) {
        if (inventory->truncated_cpu_count != UINT32_MAX)
            ++inventory->truncated_cpu_count;
        return;
    }
    inventory->apic_ids[inventory->usable_cpu_count++] = apic_id;
}

bool x86_acpi_parse_madt(const void *table, size_t available_length,
                         x86_acpi_cpu_inventory_t *inventory) {
    if (inventory == NULL) return false;
    memset(inventory, 0, sizeof(*inventory));
    inventory->version = X86_ACPI_CPU_INVENTORY_VERSION;
    inventory->struct_size = sizeof(*inventory);
    if (table == NULL || available_length < sizeof(acpi_madt_t)) return false;
    const acpi_madt_t *madt = table;
    if (memcmp(madt->header.signature, "APIC", 4U) != 0 ||
        madt->header.length < sizeof(*madt) ||
        madt->header.length > available_length ||
        madt->header.length > ACPI_TABLE_MAX_LENGTH ||
        !checksum_valid(madt, madt->header.length)) return false;
    inventory->local_apic_address = madt->local_apic_address;

    size_t offset = sizeof(*madt);
    uint32_t structure_count = 0U;
    while (offset < madt->header.length &&
           structure_count < ACPI_MAX_MADT_STRUCTURES) {
        if (madt->header.length - offset < 2U) return false;
        const uint8_t *entry = (const uint8_t *)madt + offset;
        uint8_t type = entry[0];
        uint8_t length = entry[1];
        if (length < 2U || length > madt->header.length - offset)
            return false;
        if (type == 0U) {
            if (length < 8U) return false;
            uint32_t flags = 0U;
            memcpy(&flags, entry + 4U, sizeof(flags));
            if ((flags & 1U) != 0U) cpu_inventory_add(inventory, entry[3U]);
        } else if (type == 5U) {
            if (length < 12U) return false;
            uint64_t address = 0U;
            memcpy(&address, entry + 4U, sizeof(address));
            if (address > UINT32_MAX) return false;
            inventory->local_apic_address = (uint32_t)address;
        } else if (type == 9U) {
            if (length < 16U) return false;
            uint32_t apic_id = 0U;
            uint32_t flags = 0U;
            memcpy(&apic_id, entry + 4U, sizeof(apic_id));
            memcpy(&flags, entry + 8U, sizeof(flags));
            if ((flags & 1U) != 0U) cpu_inventory_add(inventory, apic_id);
        }
        offset += length;
        ++structure_count;
    }
    return offset == madt->header.length &&
        inventory->local_apic_address != 0U &&
        inventory->usable_cpu_count != 0U;
}

bool x86_acpi_cpu_inventory(x86_acpi_cpu_inventory_t *inventory) {
    if (inventory == NULL) return false;
    const acpi_sdt_header_t *madt = find_sdt("APIC");
    if (madt == NULL) {
        memset(inventory, 0, sizeof(*inventory));
        inventory->version = X86_ACPI_CPU_INVENTORY_VERSION;
        inventory->struct_size = sizeof(*inventory);
        return false;
    }
    return x86_acpi_parse_madt(madt, madt->length, inventory);
}

bool x86_acpi_parse_dmar(const void *table, size_t available_length,
                         x86_acpi_iommu_inventory_t *inventory) {
    if (inventory == NULL) return false;
    inventory->dmar_valid = 0U;
    inventory->remapping_unit_count = 0U;
    inventory->interrupt_remapping_reported = 0U;
    if (table == NULL || available_length < sizeof(acpi_dmar_t)) return false;
    const acpi_dmar_t *dmar = table;
    if (memcmp(dmar->header.signature, "DMAR", 4U) != 0 ||
        dmar->header.length < sizeof(*dmar) ||
        dmar->header.length > available_length ||
        dmar->header.length > ACPI_TABLE_MAX_LENGTH ||
        !checksum_valid(dmar, dmar->header.length)) return false;
    for (uint32_t index = 0U; index < sizeof(dmar->reserved); ++index)
        if (dmar->reserved[index] != 0U) return false;

    size_t offset = sizeof(*dmar);
    uint32_t structure_count = 0U;
    while (offset < dmar->header.length &&
           structure_count < ACPI_MAX_DMAR_STRUCTURES) {
        if (dmar->header.length - offset < sizeof(acpi_dmar_structure_t))
            return false;
        const acpi_dmar_structure_t *structure =
            (const acpi_dmar_structure_t *)((const uint8_t *)dmar + offset);
        if (structure->length < sizeof(*structure) ||
            structure->length > dmar->header.length - offset) return false;
        if (structure->type == 0U) {
            if (structure->length < 16U) return false;
            if (inventory->remapping_unit_count == UINT32_MAX) return false;
            ++inventory->remapping_unit_count;
        }
        offset += structure->length;
        ++structure_count;
    }
    if (offset != dmar->header.length) return false;
    inventory->dmar_valid = 1U;
    inventory->interrupt_remapping_reported = (dmar->flags & 1U) != 0U;
    return true;
}

bool x86_acpi_iommu_inventory(x86_acpi_iommu_inventory_t *inventory) {
    if (inventory == NULL) return false;
    memset(inventory, 0, sizeof(*inventory));
    inventory->version = X86_ACPI_IOMMU_INVENTORY_VERSION;
    inventory->struct_size = sizeof(*inventory);
    const acpi_rsdp_t *rsdp = find_rsdp();
    uint32_t entry_width = 0U;
    const acpi_sdt_header_t *root = find_root(rsdp, &entry_width);
    if (root == NULL || entry_width == 0U) return false;
    size_t payload_length = root->length - sizeof(*root);
    if (payload_length % entry_width != 0U ||
        payload_length / entry_width > ACPI_MAX_ROOT_ENTRIES) return false;
    inventory->acpi_root_valid = 1U;

    const uint8_t *entries = (const uint8_t *)root + sizeof(*root);
    uint32_t count = (uint32_t)(payload_length / entry_width);
    for (uint32_t index = 0U; index < count; ++index) {
        uint64_t address = 0U;
        memcpy(&address, entries + (size_t)index * entry_width, entry_width);
        if (!physical_range_valid(address, sizeof(acpi_sdt_header_t)))
            continue;
        const acpi_sdt_header_t *header =
            (const acpi_sdt_header_t *)(uintptr_t)(uint32_t)address;
        if (memcmp(header->signature, "DMAR", 4U) != 0) continue;
        inventory->dmar_present = 1U;
        if (header->length < sizeof(acpi_dmar_t) ||
            header->length > ACPI_TABLE_MAX_LENGTH ||
            !physical_range_valid(address, header->length)) return true;
        (void)x86_acpi_parse_dmar(header, header->length, inventory);
        return true;
    }
    return true;
}
