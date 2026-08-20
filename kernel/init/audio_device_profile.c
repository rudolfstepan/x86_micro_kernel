/**
 * @file kernel/init/audio_device_profile.c
 * @brief Fail-closed HDA register policy construction.
 *
 * Ring 0 reads immutable controller capabilities to construct a bounded
 * mediation policy. It does not reset, enumerate or program an HDA codec.
 */
#include "kernel/init/audio_device_profile.h"

#include "drivers/bus/pci.h"
#include "include/kernel/device_domain.h"
#include "lib/libc/string.h"

#define HDA_CLASS_MULTIMEDIA 0x04U
#define HDA_SUBCLASS_AUDIO 0x03U
#define HDA_PROGRAMMING_INTERFACE 0x00U
#define HDA_BAR_INDEX 0U
#define HDA_GCAP 0x00U
#define HDA_VMAJ 0x03U
#define HDA_GCTL 0x08U
#define HDA_STATESTS 0x0EU
#define HDA_INTCTL 0x20U
#define HDA_ICOI 0x60U
#define HDA_ICIS 0x68U
#define HDA_STREAM_BASE 0x80U
#define HDA_STREAM_STRIDE 0x20U
#define HDA_SD_CTL 0x00U
#define HDA_SD_STS 0x03U
#define HDA_SD_CBL 0x08U
#define HDA_SD_LVI 0x0CU
#define HDA_SD_FMT 0x12U
#define HDA_SD_BDL 0x18U
#define HDA_PROFILE_MAP_BYTES 4096U
#define HDA_VMWARE_VENDOR_ID 0x15ADU
#define HDA_VMWARE_DEVICE_ID 0x1977U

static uint16_t hda_read16(const volatile uint8_t *base, uint32_t offset) {
    return *(const volatile uint16_t *)(base + offset);
}

static bool hda_bar0_base(const pci_device_t *device, uint32_t *base_out) {
    if (device == NULL || base_out == NULL) return false;
    uint32_t low = device->bar[HDA_BAR_INDEX];
    if (low == 0U || low == UINT32_MAX || (low & 1U) != 0U) return false;
    uint32_t type = low & 6U;
    if (type != 0U && type != 4U) return false;
    if (type == 4U && device->bar[1] != 0U) return false;
    uint32_t base = low & ~0x0FU;
    if (base == 0U || base > UINT32_MAX - HDA_PROFILE_MAP_BYTES) return false;
    *base_out = base;
    return true;
}

int audio_device_profile_discover(audio_device_profile_info_t *info) {
    if (info == NULL) return -22;
    memset(info, 0, sizeof(*info));
    for (size_t index = 0U; index < pci_device_count; ++index) {
        const pci_device_t *device = &pci_devices[index];
        if (device->class_code != HDA_CLASS_MULTIMEDIA ||
            device->subclass_code != HDA_SUBCLASS_AUDIO ||
            device->prog_if != HDA_PROGRAMMING_INTERFACE ||
            device->owner != PCI_OWNER_UNBOUND) continue;
        uint32_t base = 0U;
        if (!hda_bar0_base(device, &base)) return -95;
        const volatile uint8_t *registers =
            (const volatile uint8_t *)map_mmio_region(
                base, HDA_PROFILE_MAP_BYTES);
        if (registers == NULL || registers[HDA_VMAJ] != 1U) return -95;
        uint16_t gcap = hda_read16(registers, HDA_GCAP);
        uint32_t input_streams = (gcap >> 8U) & 0x0FU;
        uint32_t output_streams = (gcap >> 12U) & 0x0FU;
        if (output_streams == 0U || input_streams + output_streams > 30U)
            return -95;
        uint32_t output_base = HDA_STREAM_BASE +
            input_streams * HDA_STREAM_STRIDE;
        uint32_t readable = output_base + HDA_STREAM_STRIDE;
        if (readable > HDA_PROFILE_MAP_BYTES) return -95;

        const device_domain_profile_t profile = {
            .version = DEVICE_DOMAIN_ABI_VERSION,
            .struct_size = sizeof(profile),
            .isolation_group = DEVICE_DOMAIN_MAX_GROUPS - 1U,
            .flags = DEVICE_DOMAIN_PROFILE_MEDIATED_DMA |
                ((device->vendor_id == HDA_VMWARE_VENDOR_ID &&
                  device->device_id == HDA_VMWARE_DEVICE_ID)
                    ? DEVICE_DOMAIN_PROFILE_LEGACY_INTX_PIC : 0U),
            .vendor_id = device->vendor_id,
            .device_id = device->device_id,
            .class_code = device->class_code,
            .subclass_code = device->subclass_code,
            .prog_if = device->prog_if,
        };
        uint32_t device_index = 0U;
        int result = device_domain_register(
            &profile, pci_location(device), &device_index);
        if (result != 0) return result;
        uint32_t stream_interrupts = 1U << input_streams;
        const device_domain_region_policy_t policy = {
            .version = DEVICE_DOMAIN_ABI_VERSION,
            .struct_size = sizeof(policy),
            .readable_bytes = {readable},
            .rule_count = 12U,
            .rules = {
                [0] = {HDA_BAR_INDEX, HDA_GCTL, 4U,
                    DEVICE_DOMAIN_REGION_RULE_VALUE, 0x00000001U, 0U},
                [1] = {HDA_BAR_INDEX, HDA_STATESTS, 2U,
                    DEVICE_DOMAIN_REGION_RULE_VALUE, 0x00007FFFU, 0U},
                [2] = {HDA_BAR_INDEX, HDA_INTCTL, 4U,
                    DEVICE_DOMAIN_REGION_RULE_VALUE,
                    0xC0000000U | stream_interrupts, 0U},
                [3] = {HDA_BAR_INDEX, HDA_ICOI, 4U,
                    DEVICE_DOMAIN_REGION_RULE_VALUE, UINT32_MAX, 0U},
                [4] = {HDA_BAR_INDEX, HDA_ICIS, 2U,
                    DEVICE_DOMAIN_REGION_RULE_VALUE, 0x00000003U, 0U},
                [5] = {HDA_BAR_INDEX, output_base + HDA_SD_CTL, 2U,
                    DEVICE_DOMAIN_REGION_RULE_VALUE, 0x0000001FU, 0U},
                [6] = {HDA_BAR_INDEX, output_base + HDA_SD_CTL + 2U, 1U,
                    DEVICE_DOMAIN_REGION_RULE_VALUE, 0x000000F0U, 0U},
                [7] = {HDA_BAR_INDEX, output_base + HDA_SD_STS, 1U,
                    DEVICE_DOMAIN_REGION_RULE_VALUE, 0x0000001CU, 0U},
                [8] = {HDA_BAR_INDEX, output_base + HDA_SD_CBL, 4U,
                    DEVICE_DOMAIN_REGION_RULE_VALUE, UINT32_MAX, 0U},
                [9] = {HDA_BAR_INDEX, output_base + HDA_SD_LVI, 2U,
                    DEVICE_DOMAIN_REGION_RULE_VALUE, 0x000000FFU, 0U},
                [10] = {HDA_BAR_INDEX, output_base + HDA_SD_FMT, 2U,
                    DEVICE_DOMAIN_REGION_RULE_VALUE, 0x00000011U, 0U},
                [11] = {HDA_BAR_INDEX, output_base + HDA_SD_BDL, 8U,
                    DEVICE_DOMAIN_REGION_RULE_DMA_DESCRIPTOR_ADDRESS, 0U, 0U},
            },
        };
        result = device_domain_install_region_policy(device_index, &policy);
        if (result != 0) return result;
        *info = (audio_device_profile_info_t){
            .device_index = device_index,
            .pci_location = pci_location(device),
            .output_stream_base = output_base,
            .vendor_id = device->vendor_id,
            .device_id = device->device_id,
            .gcap = gcap,
            .input_streams = (uint8_t)input_streams,
            .output_streams = (uint8_t)output_streams,
        };
        return 1;
    }
    return 0;
}
