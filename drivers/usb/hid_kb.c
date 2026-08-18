/**
 * @file drivers/usb/hid_kb.c
 * @brief USB-HID-Boot-Reports als gemeinsame Tastaturereignisse.
 *
 * Layer: Ring-0 USB class driver.
 * Contract: Ein aktives Gerät besitzt genau eine generationsgebundene Reporthistorie.
 * Safety: Feste Reportgröße und Usage-Tabellen verhindern ungebundene Deskriptorinterpretation.
 */
#include "hid_kb.h"
#include "drivers/char/kb.h"
#include "lib/libc/string.h"

typedef struct {
    uint8_t scan;
    bool extended;
} hid_key_t;

static uint8_t previous_report[8];
static uint32_t active_generation;
static bool attached;

static hid_key_t hid_usage_to_key(uint8_t usage) {
    static const uint8_t letters[26] = {
        0x1EU, 0x30U, 0x2EU, 0x20U, 0x12U, 0x21U, 0x22U, 0x23U,
        0x17U, 0x24U, 0x25U, 0x26U, 0x32U, 0x31U, 0x18U, 0x19U,
        0x10U, 0x13U, 0x1FU, 0x14U, 0x16U, 0x2FU, 0x11U, 0x2DU,
        0x15U, 0x2CU
    };
    if (usage >= 4U && usage <= 29U)
        return (hid_key_t){letters[usage - 4U], false};
    if (usage >= 30U && usage <= 38U)
        return (hid_key_t){(uint8_t)(usage - 28U), false};
    if (usage == 39U) return (hid_key_t){0x0BU, false};
    if (usage >= 58U && usage <= 67U)
        return (hid_key_t){(uint8_t)(0x3BU + usage - 58U), false};
    switch (usage) {
        case 40U: return (hid_key_t){0x1CU, false};
        case 41U: return (hid_key_t){0x01U, false};
        case 42U: return (hid_key_t){0x0EU, false};
        case 43U: return (hid_key_t){0x0FU, false};
        case 44U: return (hid_key_t){0x39U, false};
        case 45U: return (hid_key_t){0x0CU, false};
        case 46U: return (hid_key_t){0x0DU, false};
        case 47U: return (hid_key_t){0x1AU, false};
        case 48U: return (hid_key_t){0x1BU, false};
        case 49U: return (hid_key_t){0x2BU, false};
        case 51U: return (hid_key_t){0x27U, false};
        case 52U: return (hid_key_t){0x28U, false};
        case 53U: return (hid_key_t){0x29U, false};
        case 54U: return (hid_key_t){0x33U, false};
        case 55U: return (hid_key_t){0x34U, false};
        case 56U: return (hid_key_t){0x35U, false};
        case 57U: return (hid_key_t){0x3AU, false};
        case 68U: return (hid_key_t){0x57U, false};
        case 69U: return (hid_key_t){0x58U, false};
        case 73U: return (hid_key_t){0x52U, true};
        case 74U: return (hid_key_t){0x47U, true};
        case 75U: return (hid_key_t){0x49U, true};
        case 76U: return (hid_key_t){0x53U, true};
        case 77U: return (hid_key_t){0x4FU, true};
        case 78U: return (hid_key_t){0x51U, true};
        case 79U: return (hid_key_t){0x4DU, true};
        case 80U: return (hid_key_t){0x4BU, true};
        case 81U: return (hid_key_t){0x50U, true};
        case 82U: return (hid_key_t){0x48U, true};
        case 83U: return (hid_key_t){0x45U, false};
        case 84U: return (hid_key_t){0x35U, true};
        case 85U: return (hid_key_t){0x37U, false};
        case 86U: return (hid_key_t){0x4AU, false};
        case 87U: return (hid_key_t){0x4EU, false};
        case 88U: return (hid_key_t){0x1CU, true};
        default: return (hid_key_t){0U, false};
    }
}

static hid_key_t hid_modifier_to_key(uint8_t bit) {
    switch (bit) {
        case 0U: return (hid_key_t){0x1DU, false};
        case 1U: return (hid_key_t){0x2AU, false};
        case 2U: return (hid_key_t){0x38U, false};
        case 4U: return (hid_key_t){0x1DU, true};
        case 5U: return (hid_key_t){0x36U, false};
        case 6U: return (hid_key_t){0x38U, true};
        default: return (hid_key_t){0U, false};
    }
}

static bool report_contains(const uint8_t report[8], uint8_t usage) {
    for (uint32_t index = 2U; index < 8U; index++)
        if (report[index] == usage) return true;
    return false;
}

static void publish(hid_key_t key, bool released) {
    if (key.scan != 0U)
        kb_submit_key_event(key.scan, key.extended, released);
}

void hid_keyboard_attach(uint32_t generation) {
    memset(previous_report, 0, sizeof(previous_report));
    active_generation = generation;
    attached = generation != 0U;
}

void hid_keyboard_detach(uint32_t generation) {
    if (!attached || generation != active_generation) return;
    for (uint8_t bit = 0U; bit < 8U; bit++)
        if (previous_report[0] & (1U << bit))
            publish(hid_modifier_to_key(bit), true);
    for (uint32_t index = 2U; index < 8U; index++)
        if (previous_report[index] >= 4U)
            publish(hid_usage_to_key(previous_report[index]), true);
    memset(previous_report, 0, sizeof(previous_report));
    attached = false;
    active_generation = 0U;
}

bool hid_keyboard_report(uint32_t generation, const uint8_t *report,
                         size_t length) {
    if (!attached || generation != active_generation || !report ||
        length != sizeof(previous_report) || report[1] != 0U)
        return false;
    for (uint32_t index = 2U; index < 8U; index++)
        if (report[index] >= 1U && report[index] <= 3U) return false;

    for (uint32_t index = 2U; index < 8U; index++) {
        uint8_t usage = previous_report[index];
        if (usage >= 4U && !report_contains(report, usage))
            publish(hid_usage_to_key(usage), true);
    }
    uint8_t modifier_changes = previous_report[0] ^ report[0];
    for (uint8_t bit = 0U; bit < 8U; bit++)
        if (modifier_changes & (1U << bit))
            publish(hid_modifier_to_key(bit),
                    (report[0] & (1U << bit)) == 0U);
    for (uint32_t index = 2U; index < 8U; index++) {
        uint8_t usage = report[index];
        if (usage >= 4U && !report_contains(previous_report, usage))
            publish(hid_usage_to_key(usage), false);
    }
    memcpy(previous_report, report, sizeof(previous_report));
    return true;
}
