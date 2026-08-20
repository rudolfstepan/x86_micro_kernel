/**
 * @file hda_driver.c
 * @brief Supervised Ring-3 Intel High Definition Audio playback driver.
 *
 * All controller and codec state machines run here in Ring 3.  Ring 0 only
 * mediates an immutable register allow-list, IRQ delivery and sealed DMA
 * descriptors.  Every hardware wait has a finite iteration bound.
 */
#include <stddef.h>
#include <stdint.h>

#include "hda_driver.h"
#include "reist/audio.h"
#include "x86os.h"

#define HDA_GCAP 0x00U
#define HDA_GCTL 0x08U
#define HDA_STATESTS 0x0EU
#define HDA_INTCTL 0x20U
#define HDA_ICW 0x60U
#define HDA_IRR 0x64U
#define HDA_ICS 0x68U
#define HDA_STREAM_BASE 0x80U
#define HDA_STREAM_STRIDE 0x20U
#define HDA_SD_CTL 0x00U
#define HDA_SD_STS 0x03U
#define HDA_SD_CBL 0x08U
#define HDA_SD_LVI 0x0CU
#define HDA_SD_FMT 0x12U
#define HDA_SD_BDL 0x18U

#define HDA_GCTL_RESET 0x00000001U
#define HDA_ICS_BUSY 0x0001U
#define HDA_ICS_RESPONSE_VALID 0x0002U
#define HDA_SD_CTL_RESET 0x0001U
#define HDA_SD_CTL_RUN 0x0002U
#define HDA_SD_CTL_IOCE 0x0004U
#define HDA_SD_STS_ACK 0x001CU
#define HDA_STREAM_TAG 1U
#define HDA_RESET_POLLS 100U
#define HDA_VERB_POLLS 200U
#define HDA_STREAM_POLLS 100U
#define HDA_HEARTBEAT_MS 500U
#define HDA_DIAGNOSTIC_STAGE_REGION 2U
#define HDA_DIAGNOSTIC_STAGE_IRQ_ENDPOINT 3U
#define HDA_DIAGNOSTIC_STAGE_IRQ_BIND 4U
#define HDA_DIAGNOSTIC_STAGE_DMA_BIND 5U
#define HDA_DIAGNOSTIC_STAGE_DMA_INFO 6U
#define HDA_DIAGNOSTIC_STAGE_GCAP 7U
#define HDA_DIAGNOSTIC_STAGE_CONTROLLER_RESET 8U
#define HDA_DIAGNOSTIC_STAGE_CODEC_DISCOVERY 9U
#define HDA_DIAGNOSTIC_STAGE_CONTROL_CHANNEL 10U
#define HDA_DIAGNOSTIC_STAGE_CHANNEL_REPORT 11U
#define HDA_DIAGNOSTIC_STAGE_SELF_TEST 12U
#define HDA_DIAGNOSTIC_STAGE_PROGRESS 13U

#define HDA_PARAMETER_SUBORDINATE_NODES 0x04U
#define HDA_PARAMETER_FUNCTION_GROUP_TYPE 0x05U
#define HDA_PARAMETER_AUDIO_WIDGET_CAPS 0x09U
#define HDA_PARAMETER_PIN_CAPS 0x0CU
#define HDA_PARAMETER_INPUT_AMP_CAPS 0x0DU
#define HDA_PARAMETER_CONNECTION_LIST_LENGTH 0x0EU
#define HDA_PARAMETER_OUTPUT_AMP_CAPS 0x12U
#define HDA_FUNCTION_GROUP_AUDIO 0x01U
#define HDA_WIDGET_AUDIO_OUTPUT 0x00U
#define HDA_WIDGET_AUDIO_MIXER 0x02U
#define HDA_WIDGET_AUDIO_SELECTOR 0x03U
#define HDA_WIDGET_PIN_COMPLEX 0x04U
#define HDA_WIDGET_CAP_INPUT_AMP (1U << 1U)
#define HDA_WIDGET_CAP_OUTPUT_AMP (1U << 2U)
#define HDA_WIDGET_CAP_AMP_OVERRIDE (1U << 3U)
#define HDA_WIDGET_CAP_CONNECTION_LIST (1U << 8U)
#define HDA_PIN_CAP_OUTPUT (1U << 4U)
#define HDA_PIN_CAP_EAPD (1U << 16U)
#define HDA_AMP_SET_OUTPUT (1U << 15U)
#define HDA_AMP_SET_LEFT (1U << 13U)
#define HDA_AMP_SET_RIGHT (1U << 12U)
#define HDA_AMP_SET_INDEX_SHIFT 8U
#define HDA_PLAYBACK_BOOST_QUARTER_DB 24U
#define HDA_CONNECTION_CAPACITY 16U

typedef struct {
    x86os_device_driver_bootstrap_t bootstrap;
    x86os_device_resource_t registers;
    x86os_ipc_handle_t irq_endpoint;
    x86os_device_resource_t irq;
    x86os_device_resource_t dma;
    x86os_device_dma_info_t dma_info;
    x86os_ipc_handle_t control;
    uint32_t stream_base;
    uint32_t stream_index;
    uint32_t codec;
    uint32_t dac_node;
    uint32_t pin_node;
    uint32_t dac_gain_0db;
    uint32_t dac_has_output_amp;
    uint32_t pin_gain_0db;
    uint32_t pin_has_output_amp;
    uint32_t pin_caps;
    uint32_t pin_connection_count;
    uint32_t pin_connection_index;
    uint32_t path_node;
    uint32_t path_type;
    uint32_t path_connection_count;
    uint32_t path_connection_index;
    uint32_t path_input_gain;
    uint32_t path_has_input_amp;
    uint32_t path_output_gain;
    uint32_t path_has_output_amp;
    uint32_t stream_id;
    uint32_t stream_generation;
    uint32_t buffered_frames;
    uint32_t progress;
    uint32_t backend_state;
    uint32_t activated;
    uint32_t fatal;
} hda_driver_t;

bool reist_hda_decode_nodes(uint32_t response, uint8_t *start_node,
                            uint8_t *node_count) {
    if (start_node == NULL || node_count == NULL) return false;
    uint8_t start = (uint8_t)(response >> 16U);
    uint8_t count = (uint8_t)response;
    if (start == 0U || count == 0U || (uint32_t)start + count > 256U)
        return false;
    *start_node = start;
    *node_count = count;
    return true;
}

bool reist_hda_amp_0db_gain(uint32_t capabilities, uint8_t *gain) {
    if (gain == NULL) return false;
    uint32_t offset = capabilities & 0x7FU;
    uint32_t steps = (capabilities >> 8U) & 0x7FU;
    if (offset > steps) return false;
    *gain = (uint8_t)offset;
    return true;
}

/** Select a capability-bounded gain above the codec-declared 0-dB offset. */
bool reist_hda_amp_playback_gain(uint32_t capabilities, uint8_t *gain) {
    if (gain == NULL) return false;
    uint32_t offset = capabilities & 0x7FU;
    uint32_t steps = (capabilities >> 8U) & 0x7FU;
    uint32_t step_quarter_db = ((capabilities >> 16U) & 0x7FU) + 1U;
    if (offset > steps) return false;
    uint32_t boost_steps =
        HDA_PLAYBACK_BOOST_QUARTER_DB / step_quarter_db;
    if (boost_steps > steps - offset) boost_steps = steps - offset;
    *gain = (uint8_t)(offset + boost_steps);
    return true;
}

#ifndef REIST_HDA_DRIVER_HELPERS_ONLY
static void bytes_zero(void *destination, size_t length) {
    uint8_t *bytes = destination;
    for (size_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static int driver_failure(hda_driver_t *driver, uint32_t stage, int status) {
    uint32_t error = status < 0 ? (uint32_t)(-status) : (uint32_t)status;
    uint32_t diagnostic = (stage << 24U) | (error & 0x00FFFFFFU);
    (void)x86os_device_driver_report(
        &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_DIAGNOSTIC,
        diagnostic);
    return status;
}

static int register_read(const hda_driver_t *driver, uint32_t offset,
                         uint32_t width, uint32_t *value) {
    return x86os_device_region_read(driver->registers, offset, width, value);
}

static int register_write(const hda_driver_t *driver, uint32_t offset,
                          uint32_t width, uint32_t value) {
    return x86os_device_region_write(
        driver->registers, offset, width, value);
}

static int poll_bit(const hda_driver_t *driver, uint32_t offset,
                    uint32_t width, uint32_t mask, uint32_t expected,
                    uint32_t attempts) {
    for (uint32_t attempt = 0U; attempt < attempts; ++attempt) {
        uint32_t value = 0U;
        int result = register_read(driver, offset, width, &value);
        if (result != 0) return result;
        if ((value & mask) == expected) return 0;
        if (x86os_sleep_ms(1U) != 0) (void)x86os_yield();
    }
    return -110;
}

static uint32_t hda_verb12(uint32_t codec, uint32_t node,
                           uint32_t verb, uint32_t payload) {
    return (codec << 28U) | (node << 20U) |
        ((verb & 0x0FFFU) << 8U) | (payload & 0xFFU);
}

static uint32_t hda_verb4(uint32_t codec, uint32_t node,
                          uint32_t verb, uint32_t payload) {
    return (codec << 28U) | (node << 20U) |
        ((verb & 0x0FU) << 16U) | (payload & 0xFFFFU);
}

static int immediate_verb(const hda_driver_t *driver, uint32_t verb,
                          uint32_t *response) {
    int result = poll_bit(driver, HDA_ICS, 2U, HDA_ICS_BUSY, 0U,
                          HDA_VERB_POLLS);
    if (result != 0) return result;
    uint32_t status = 0U;
    result = register_read(driver, HDA_ICS, 2U, &status);
    if (result != 0) return result;
    if ((status & HDA_ICS_RESPONSE_VALID) != 0U &&
        register_write(driver, HDA_ICS, 2U,
                       HDA_ICS_RESPONSE_VALID) != 0) return -5;
    if (register_write(driver, HDA_ICW, 4U, verb) != 0 ||
        register_write(driver, HDA_ICS, 2U, HDA_ICS_BUSY) != 0) return -5;
    for (uint32_t attempt = 0U; attempt < HDA_VERB_POLLS; ++attempt) {
        result = register_read(driver, HDA_ICS, 2U, &status);
        if (result != 0) return result;
        if ((status & HDA_ICS_BUSY) == 0U &&
            (status & HDA_ICS_RESPONSE_VALID) != 0U) {
            uint32_t value = 0U;
            result = register_read(driver, HDA_IRR, 4U, &value);
            if (result == 0)
                result = register_write(driver, HDA_ICS, 2U,
                                        HDA_ICS_RESPONSE_VALID);
            if (result == 0 && response != NULL) *response = value;
            return result;
        }
        if (x86os_sleep_ms(1U) != 0) (void)x86os_yield();
    }
    return -110;
}

static int parameter(const hda_driver_t *driver, uint32_t node,
                     uint32_t parameter_id, uint32_t *response) {
    return immediate_verb(driver,
        hda_verb12(driver->codec, node, 0xF00U, parameter_id), response);
}

/** Decode one bounded HDA connection list, including standard range entries. */
static int connection_list(const hda_driver_t *driver, uint32_t node,
                           uint8_t nodes[HDA_CONNECTION_CAPACITY],
                           uint32_t *node_count) {
    uint32_t length_response = 0U;
    if (nodes == NULL || node_count == NULL ||
        parameter(driver, node, HDA_PARAMETER_CONNECTION_LIST_LENGTH,
                  &length_response) != 0) return -5;
    uint32_t raw_count = length_response & 0x7FU;
    uint32_t long_form = (length_response >> 7U) & 1U;
    if (raw_count == 0U || raw_count > HDA_CONNECTION_CAPACITY) return -95;

    uint32_t completed = 0U;
    uint32_t expanded = 0U;
    uint32_t previous = 0U;
    int have_previous = 0;
    while (completed < raw_count) {
        uint32_t response = 0U;
        if (immediate_verb(driver,
                hda_verb12(driver->codec, node, 0xF02U, completed),
                &response) != 0) return -5;
        uint32_t per_response = long_form != 0U ? 2U : 4U;
        uint32_t value_mask = long_form != 0U ? 0x7FFFU : 0x7FU;
        uint32_t range_mask = long_form != 0U ? 0x8000U : 0x80U;
        uint32_t shift_step = long_form != 0U ? 16U : 8U;
        for (uint32_t slot = 0U;
             slot < per_response && completed < raw_count;
             ++slot, ++completed) {
            uint32_t entry = (response >> (slot * shift_step)) &
                (value_mask | range_mask);
            uint32_t target = entry & value_mask;
            if (target == 0U || target > 255U) return -84;
            if ((entry & range_mask) != 0U) {
                if (!have_previous || target <= previous) return -84;
                for (uint32_t ranged = previous + 1U; ranged <= target;
                     ++ranged) {
                    if (expanded >= HDA_CONNECTION_CAPACITY) return -95;
                    nodes[expanded++] = (uint8_t)ranged;
                }
            } else {
                if (expanded >= HDA_CONNECTION_CAPACITY) return -95;
                nodes[expanded++] = (uint8_t)target;
            }
            previous = target;
            have_previous = 1;
        }
    }
    *node_count = expanded;
    return 0;
}

static int widget_amp_gain(const hda_driver_t *driver,
                           uint32_t function_group, uint32_t node,
                           uint32_t widget_caps, uint32_t parameter_id,
                           uint32_t *gain) {
    uint32_t amp_caps = 0U;
    uint32_t amp_node =
        (widget_caps & HDA_WIDGET_CAP_AMP_OVERRIDE) != 0U
        ? node : function_group;
    if (parameter(driver, amp_node, parameter_id, &amp_caps) != 0) return -5;
    uint8_t selected = 0U;
    if (!reist_hda_amp_playback_gain(amp_caps, &selected)) return -84;
    *gain = selected;
    return 0;
}

/** Select a direct path or one standard mixer/selector between DAC and pin. */
static int playback_path_discover(hda_driver_t *driver,
                                  uint32_t function_group) {
    uint8_t pin_sources[HDA_CONNECTION_CAPACITY];
    uint32_t pin_source_count = 0U;
    int result = connection_list(
        driver, driver->pin_node, pin_sources, &pin_source_count);
    if (result != 0) return result;
    driver->pin_connection_count = pin_source_count;
    for (uint32_t pin_index = 0U; pin_index < pin_source_count; ++pin_index) {
        uint32_t source = pin_sources[pin_index];
        if (source == driver->dac_node) {
            driver->pin_connection_index = pin_index;
            return 0;
        }
        uint32_t widget_caps = 0U;
        if (parameter(driver, source, HDA_PARAMETER_AUDIO_WIDGET_CAPS,
                      &widget_caps) != 0) continue;
        uint32_t type = (widget_caps >> 20U) & 0x0FU;
        if (type != HDA_WIDGET_AUDIO_MIXER &&
            type != HDA_WIDGET_AUDIO_SELECTOR) continue;
        uint8_t path_sources[HDA_CONNECTION_CAPACITY];
        uint32_t path_source_count = 0U;
        if (connection_list(driver, source, path_sources,
                            &path_source_count) != 0) continue;
        for (uint32_t path_index = 0U; path_index < path_source_count;
             ++path_index) {
            if (path_sources[path_index] != driver->dac_node) continue;
            driver->pin_connection_index = pin_index;
            driver->path_node = source;
            driver->path_type = type;
            driver->path_connection_count = path_source_count;
            driver->path_connection_index = path_index;
            if ((widget_caps & HDA_WIDGET_CAP_INPUT_AMP) != 0U) {
                result = widget_amp_gain(
                    driver, function_group, source, widget_caps,
                    HDA_PARAMETER_INPUT_AMP_CAPS, &driver->path_input_gain);
                if (result != 0) return result;
                driver->path_has_input_amp = 1U;
            }
            if ((widget_caps & HDA_WIDGET_CAP_OUTPUT_AMP) != 0U) {
                result = widget_amp_gain(
                    driver, function_group, source, widget_caps,
                    HDA_PARAMETER_OUTPUT_AMP_CAPS, &driver->path_output_gain);
                if (result != 0) return result;
                driver->path_has_output_amp = 1U;
            }
            return 0;
        }
    }
    return -19;
}

static int controller_reset(hda_driver_t *driver) {
    if (register_write(driver, HDA_GCTL, 4U, 0U) != 0) return -5;
    int result = poll_bit(driver, HDA_GCTL, 4U, HDA_GCTL_RESET, 0U,
                          HDA_RESET_POLLS);
    if (result != 0 ||
        register_write(driver, HDA_GCTL, 4U, HDA_GCTL_RESET) != 0) return -5;
    result = poll_bit(driver, HDA_GCTL, 4U, HDA_GCTL_RESET,
                      HDA_GCTL_RESET, HDA_RESET_POLLS);
    if (result != 0) return result;
    uint32_t codecs = 0U;
    for (uint32_t attempt = 0U; attempt < HDA_RESET_POLLS; ++attempt) {
        result = register_read(driver, HDA_STATESTS, 2U, &codecs);
        if (result != 0) return result;
        codecs &= 0x7FFFU;
        if (codecs != 0U) break;
        if (x86os_sleep_ms(1U) != 0) (void)x86os_yield();
    }
    if (codecs == 0U) return -19;
    for (uint32_t codec = 0U; codec < 15U; ++codec) {
        if ((codecs & (1U << codec)) != 0U) {
            driver->codec = codec;
            return 0;
        }
    }
    return -19;
}

static int codec_discover(hda_driver_t *driver) {
    uint32_t response = 0U;
    uint8_t start = 0U;
    uint8_t count = 0U;
    if (parameter(driver, 0U, HDA_PARAMETER_SUBORDINATE_NODES, &response) != 0 ||
        !reist_hda_decode_nodes(response, &start, &count)) return -19;
    uint32_t function_group = 0U;
    for (uint32_t node = start; node < (uint32_t)start + count; ++node) {
        if (parameter(driver, node, HDA_PARAMETER_FUNCTION_GROUP_TYPE,
                      &response) == 0 &&
            (response & 0xFFU) == HDA_FUNCTION_GROUP_AUDIO) {
            function_group = node;
            break;
        }
    }
    if (function_group == 0U ||
        parameter(driver, function_group, HDA_PARAMETER_SUBORDINATE_NODES,
                  &response) != 0 ||
        !reist_hda_decode_nodes(response, &start, &count)) return -19;
    for (uint32_t node = start; node < (uint32_t)start + count; ++node) {
        if (parameter(driver, node, HDA_PARAMETER_AUDIO_WIDGET_CAPS,
                      &response) != 0) continue;
        uint32_t type = (response >> 20U) & 0x0FU;
        if (type == HDA_WIDGET_AUDIO_OUTPUT && driver->dac_node == 0U) {
            driver->dac_node = node;
            if ((response & HDA_WIDGET_CAP_OUTPUT_AMP) != 0U) {
                int result = widget_amp_gain(
                    driver, function_group, node, response,
                    HDA_PARAMETER_OUTPUT_AMP_CAPS, &driver->dac_gain_0db);
                if (result != 0) return result;
                driver->dac_has_output_amp = 1U;
            }
        }
        if (type == HDA_WIDGET_PIN_COMPLEX && driver->pin_node == 0U) {
            uint32_t pin_caps = 0U;
            if (parameter(driver, node, HDA_PARAMETER_PIN_CAPS,
                          &pin_caps) == 0 &&
                (pin_caps & HDA_PIN_CAP_OUTPUT) != 0U) {
                driver->pin_node = node;
                driver->pin_caps = pin_caps;
                if ((response & HDA_WIDGET_CAP_OUTPUT_AMP) != 0U) {
                    int result = widget_amp_gain(
                        driver, function_group, node, response,
                        HDA_PARAMETER_OUTPUT_AMP_CAPS,
                        &driver->pin_gain_0db);
                    if (result != 0) return result;
                    driver->pin_has_output_amp = 1U;
                }
            }
        }
    }
    if (driver->dac_node == 0U || driver->pin_node == 0U) return -19;
    return playback_path_discover(driver, function_group);
}

static int codec_configure(const hda_driver_t *driver) {
    uint32_t ignored = 0U;
    const uint32_t required_verbs[] = {
        hda_verb12(driver->codec, driver->dac_node, 0x705U, 0U),
        hda_verb12(driver->codec, driver->pin_node, 0x705U, 0U),
        hda_verb4(driver->codec, driver->dac_node, 0x2U,
                  REIST_HDA_STREAM_FORMAT_S16_STEREO_48K),
        hda_verb12(driver->codec, driver->dac_node, 0x706U,
                   HDA_STREAM_TAG << 4U),
        hda_verb12(driver->codec, driver->pin_node, 0x707U, 0x40U),
    };
    for (size_t index = 0U;
         index < sizeof(required_verbs) / sizeof(required_verbs[0]); ++index)
        if (immediate_verb(driver, required_verbs[index], &ignored) != 0)
            return -5;
    if (driver->path_node != 0U &&
        immediate_verb(driver,
            hda_verb12(driver->codec, driver->path_node, 0x705U, 0U),
            &ignored) != 0) return -5;
    /* A single hard-wired connection has no Connection Select Control. */
    if (driver->pin_connection_count > 1U &&
        immediate_verb(driver,
            hda_verb12(driver->codec, driver->pin_node, 0x701U,
                       driver->pin_connection_index),
            &ignored) != 0) return -5;
    if (driver->path_type == HDA_WIDGET_AUDIO_SELECTOR &&
        driver->path_connection_count > 1U &&
        immediate_verb(driver,
            hda_verb12(driver->codec, driver->path_node, 0x701U,
                       driver->path_connection_index),
            &ignored) != 0) return -5;
    if ((driver->pin_caps & HDA_PIN_CAP_EAPD) != 0U &&
        immediate_verb(driver,
            hda_verb12(driver->codec, driver->pin_node, 0x70CU, 0x02U),
            &ignored) != 0) return -5;
    if (driver->dac_has_output_amp != 0U) {
        uint32_t payload = HDA_AMP_SET_OUTPUT | HDA_AMP_SET_LEFT |
            HDA_AMP_SET_RIGHT | driver->dac_gain_0db;
        if (immediate_verb(driver,
                hda_verb4(driver->codec, driver->dac_node, 0x3U, payload),
                &ignored) != 0) return -5;
    }
    if (driver->pin_has_output_amp != 0U) {
        uint32_t payload = HDA_AMP_SET_OUTPUT | HDA_AMP_SET_LEFT |
            HDA_AMP_SET_RIGHT | driver->pin_gain_0db;
        if (immediate_verb(driver,
                hda_verb4(driver->codec, driver->pin_node, 0x3U, payload),
                &ignored) != 0) return -5;
    }
    if (driver->path_has_output_amp != 0U) {
        uint32_t payload = HDA_AMP_SET_OUTPUT | HDA_AMP_SET_LEFT |
            HDA_AMP_SET_RIGHT | driver->path_output_gain;
        if (immediate_verb(driver,
                hda_verb4(driver->codec, driver->path_node, 0x3U, payload),
                &ignored) != 0) return -5;
    }
    if (driver->path_has_input_amp != 0U) {
        uint32_t payload = HDA_AMP_SET_LEFT | HDA_AMP_SET_RIGHT |
            (driver->path_connection_index << HDA_AMP_SET_INDEX_SHIFT) |
            driver->path_input_gain;
        if (immediate_verb(driver,
                hda_verb4(driver->codec, driver->path_node, 0x3U, payload),
                &ignored) != 0) return -5;
    }
    return 0;
}

static int stream_identity_valid(const hda_driver_t *driver,
                                 const reist_audio_message_t *request) {
    return driver->stream_id != 0U &&
        request->stream_id == driver->stream_id &&
        request->stream_generation == driver->stream_generation;
}

static int stream_start(hda_driver_t *driver) {
    if (driver->backend_state != REIST_AUDIO_BACKEND_BUFFERING ||
        driver->buffered_frames == 0U || driver->activated != 0U) return -16;
    uint32_t byte_count = driver->buffered_frames *
        REIST_AUDIO_CHANNELS * sizeof(int16_t);
    if (x86os_device_dma_descriptor_set(
            driver->dma, 0U, X86OS_DEVICE_DMA_DATA_OFFSET, byte_count,
            X86OS_DEVICE_DMA_DESCRIPTOR_INTERRUPT) != 0 ||
        x86os_device_region_bind_dma(
            driver->registers, driver->dma,
            driver->stream_base + HDA_SD_BDL, 0U) != 0) return -5;
    uint32_t ctl = driver->stream_base + HDA_SD_CTL;
    if (register_write(driver, ctl, 2U, HDA_SD_CTL_RESET) != 0 ||
        poll_bit(driver, ctl, 2U, HDA_SD_CTL_RESET, HDA_SD_CTL_RESET,
                 HDA_STREAM_POLLS) != 0 ||
        register_write(driver, ctl, 2U, 0U) != 0 ||
        poll_bit(driver, ctl, 2U, HDA_SD_CTL_RESET, 0U,
                 HDA_STREAM_POLLS) != 0 ||
        register_write(driver, driver->stream_base + HDA_SD_STS, 1U,
                       HDA_SD_STS_ACK) != 0 ||
        register_write(driver, driver->stream_base + HDA_SD_CBL, 4U,
                       byte_count) != 0 ||
        register_write(driver, driver->stream_base + HDA_SD_LVI, 2U, 0U) != 0 ||
        register_write(driver, driver->stream_base + HDA_SD_FMT, 2U,
                       REIST_HDA_STREAM_FORMAT_S16_STEREO_48K) != 0 ||
        register_write(driver, ctl + 2U, 1U, HDA_STREAM_TAG << 4U) != 0 ||
        codec_configure(driver) != 0) return -5;
    uint32_t interrupt_mask = 0xC0000000U | (1U << driver->stream_index);
    if (register_write(driver, HDA_INTCTL, 4U, interrupt_mask) != 0 ||
        x86os_device_activate(driver->bootstrap.device) != 0 ||
        register_write(driver, ctl, 2U,
                       HDA_SD_CTL_RUN | HDA_SD_CTL_IOCE) != 0) return -5;
    driver->activated = 1U;
    driver->backend_state = REIST_AUDIO_BACKEND_RUNNING;
    return 0;
}

static int stream_stop(hda_driver_t *driver) {
    if (driver->backend_state != REIST_AUDIO_BACKEND_RUNNING) return -16;
    uint32_t ctl = driver->stream_base + HDA_SD_CTL;
    if (register_write(driver, ctl, 2U, HDA_SD_CTL_IOCE) != 0 ||
        poll_bit(driver, ctl, 2U, HDA_SD_CTL_RUN, 0U,
                 HDA_STREAM_POLLS) != 0 ||
        register_write(driver, HDA_INTCTL, 4U, 0U) != 0) return -5;
    int result = x86os_device_deactivate(driver->bootstrap.device);
    driver->activated = 0U;
    driver->backend_state = REIST_AUDIO_BACKEND_READY;
    return result;
}

static int stream_abandon(hda_driver_t *driver) {
    int result = 0;
    if (driver->backend_state == REIST_AUDIO_BACKEND_RUNNING)
        result = stream_stop(driver);
    driver->stream_id = 0U;
    driver->stream_generation = 0U;
    driver->buffered_frames = 0U;
    if (result == 0) driver->backend_state = REIST_AUDIO_BACKEND_READY;
    return result;
}

static void response_prepare(reist_audio_message_t *response,
                             const reist_audio_message_t *request,
                             int status) {
    bytes_zero(response, sizeof(*response));
    response->version = REIST_AUDIO_PROTOCOL_VERSION;
    response->struct_size = sizeof(*response);
    response->command = request->command | REIST_AUDIO_RESPONSE_FLAG;
    response->request_id = request->request_id;
    response->stream_id = request->stream_id;
    response->stream_generation = request->stream_generation;
    response->status = status;
}

static int handle_request(hda_driver_t *driver,
                          const reist_audio_message_t *request,
                          reist_audio_message_t *response) {
    int status = -22;
    response_prepare(response, request, status);
    if (request->version != REIST_AUDIO_PROTOCOL_VERSION ||
        request->struct_size != sizeof(*request) || request->request_id == 0U ||
        (request->command & REIST_AUDIO_RESPONSE_FLAG) != 0U) return -22;
    if (request->command == REIST_AUDIO_COMMAND_INFO) {
        status = 0;
        response->payload.words[0] = REIST_AUDIO_SAMPLE_RATE;
        response->payload.words[1] = REIST_AUDIO_CHANNELS;
        response->payload.words[2] = REIST_AUDIO_FORMAT_S16_LE;
        response->payload.words[3] = REIST_AUDIO_MESSAGE_FRAMES;
        response->payload.words[4] = REIST_AUDIO_MAX_STREAM_FRAMES;
        response->payload.words[5] = driver->backend_state;
    } else if (request->command == REIST_AUDIO_COMMAND_OPEN) {
        if (driver->stream_id != 0U || driver->activated != 0U)
            status = -16;
        else if (request->stream_id == 0U ||
                 request->stream_generation == 0U ||
                 request->payload.words[0] != REIST_AUDIO_SAMPLE_RATE ||
                 request->payload.words[1] != REIST_AUDIO_CHANNELS ||
                 request->payload.words[2] != REIST_AUDIO_FORMAT_S16_LE)
            status = -22;
        else {
            driver->stream_id = request->stream_id;
            driver->stream_generation = request->stream_generation;
            driver->buffered_frames = 0U;
            driver->backend_state = REIST_AUDIO_BACKEND_BUFFERING;
            status = 0;
        }
    } else if (!stream_identity_valid(driver, request)) {
        status = -9;
    } else if (request->command == REIST_AUDIO_COMMAND_WRITE) {
        if (driver->backend_state != REIST_AUDIO_BACKEND_BUFFERING ||
            request->frame_count == 0U ||
            request->frame_count > REIST_AUDIO_MESSAGE_FRAMES) {
            status = -22;
        } else if (driver->buffered_frames >
                REIST_AUDIO_MAX_STREAM_FRAMES - request->frame_count) {
            status = -11;
        } else {
            uint32_t offset = X86OS_DEVICE_DMA_DATA_OFFSET +
                driver->buffered_frames * REIST_AUDIO_CHANNELS *
                    sizeof(int16_t);
            uint32_t bytes = request->frame_count * REIST_AUDIO_CHANNELS *
                sizeof(int16_t);
            status = x86os_device_dma_write(
                driver->dma, offset, request->payload.samples, bytes);
            if (status == 0) driver->buffered_frames += request->frame_count;
        }
    } else if (request->command == REIST_AUDIO_COMMAND_START) {
        status = stream_start(driver);
    } else if (request->command == REIST_AUDIO_COMMAND_STOP) {
        status = stream_stop(driver);
    } else if (request->command == REIST_AUDIO_COMMAND_CLOSE) {
        if (driver->backend_state == REIST_AUDIO_BACKEND_RUNNING) status = -16;
        else {
            driver->stream_id = 0U;
            driver->stream_generation = 0U;
            driver->buffered_frames = 0U;
            status = 0;
        }
    }
    response->stream_id = request->stream_id;
    response->stream_generation = request->stream_generation;
    response->status = status;
    if (status == -5 || status == -110) driver->fatal = 1U;
    return status;
}

static int ipc_decode(const x86os_ipc_message_t *ipc,
                      reist_audio_message_t *wire) {
    if (ipc == NULL || wire == NULL ||
        ipc->version != X86OS_IPC_MESSAGE_VERSION ||
        ipc->struct_size != sizeof(*ipc) || ipc->length != sizeof(*wire))
        return -84;
    for (size_t index = 0U; index < sizeof(*wire); ++index)
        ((uint8_t *)wire)[index] = ipc->payload[index];
    return 0;
}

static void ipc_encode(x86os_ipc_message_t *ipc,
                       const reist_audio_message_t *wire) {
    bytes_zero(ipc, sizeof(*ipc));
    ipc->version = X86OS_IPC_MESSAGE_VERSION;
    ipc->struct_size = sizeof(*ipc);
    ipc->length = sizeof(*wire);
    for (size_t index = 0U; index < sizeof(*wire); ++index)
        ipc->payload[index] = ((const uint8_t *)wire)[index];
}

static void ipc_receive_prepare(x86os_ipc_message_t *ipc) {
    bytes_zero(ipc, sizeof(*ipc));
    ipc->version = X86OS_IPC_MESSAGE_VERSION;
    ipc->struct_size = sizeof(*ipc);
}

static void service_control_poll(hda_driver_t *driver) {
    x86os_ipc_message_t ipc;
    ipc_receive_prepare(&ipc);
    int received = x86os_ipc_receive_timeout(driver->control, &ipc, 20U);
    if (received == -32) {
        if (stream_abandon(driver) != 0) driver->fatal = 1U;
        return;
    }
    if (received != 0) return;
    reist_audio_message_t request;
    reist_audio_message_t response;
    if (ipc_decode(&ipc, &request) != 0) return;
    (void)handle_request(driver, &request, &response);
    ipc_encode(&ipc, &response);
    (void)x86os_ipc_send_timeout(driver->control, &ipc, 100U);
}

static void irq_poll(hda_driver_t *driver) {
    x86os_ipc_message_t ipc;
    ipc_receive_prepare(&ipc);
    if (x86os_ipc_receive_timeout(driver->irq_endpoint, &ipc, 0U) != 0 ||
        ipc.version != X86OS_IPC_MESSAGE_VERSION ||
        ipc.struct_size != sizeof(ipc) ||
        ipc.length != sizeof(x86os_device_irq_message_t)) return;
    x86os_device_irq_message_t notification;
    for (size_t index = 0U; index < sizeof(notification); ++index)
        ((uint8_t *)&notification)[index] = ipc.payload[index];
    if (notification.version != X86OS_DEVICE_ABI_VERSION ||
        notification.struct_size != sizeof(notification) ||
        notification.resource != driver->irq || notification.sequence == 0U)
        return;
    uint32_t status = 0U;
    if (register_read(driver, driver->stream_base + HDA_SD_STS, 1U,
                      &status) == 0 && (status & HDA_SD_STS_ACK) != 0U)
        (void)register_write(driver, driver->stream_base + HDA_SD_STS, 1U,
                             status & HDA_SD_STS_ACK);
    x86os_device_irq_completion_t completion;
    (void)x86os_device_irq_complete(driver->irq, &completion);
}

static int driver_initialize(hda_driver_t *driver) {
    bytes_zero(driver, sizeof(*driver));
    driver->backend_state = REIST_AUDIO_BACKEND_READY;
    if (x86os_device_driver_bootstrap(&driver->bootstrap) != 0 ||
        driver->bootstrap.mode != X86OS_DEVICE_MODE_MEDIATED) return -13;
    x86os_device_region_info_t region;
    int status = x86os_device_open_region(
            driver->bootstrap.device, 0U,
            X86OS_DEVICE_REGION_ACCESS_READ |
                X86OS_DEVICE_REGION_ACCESS_WRITE,
            &region);
    if (status != 0)
        return driver_failure(driver, HDA_DIAGNOSTIC_STAGE_REGION, status);
    driver->registers = region.resource;
    status = x86os_ipc_create(&driver->irq_endpoint);
    if (status != 0)
        return driver_failure(
            driver, HDA_DIAGNOSTIC_STAGE_IRQ_ENDPOINT, status);
    x86os_device_resource_result_t resource;
    status = x86os_device_bind_irq(driver->bootstrap.device,
                                   driver->irq_endpoint, &resource);
    if (status != 0)
        return driver_failure(driver, HDA_DIAGNOSTIC_STAGE_IRQ_BIND, status);
    driver->irq = resource.resource;
    status = x86os_device_bind_dma_direction(
        driver->bootstrap.device, 0U, X86OS_DEVICE_DMA_TO_DEVICE, &resource);
    if (status != 0)
        return driver_failure(driver, HDA_DIAGNOSTIC_STAGE_DMA_BIND, status);
    driver->dma = resource.resource;
    status = x86os_device_dma_info(driver->dma, &driver->dma_info);
    if (status != 0 ||
        driver->dma_info.capacity <= X86OS_DEVICE_DMA_DATA_OFFSET ||
        driver->dma_info.reserved[0] != 0U ||
        driver->dma_info.reserved[1] != 0U)
        return driver_failure(driver, HDA_DIAGNOSTIC_STAGE_DMA_INFO,
                              status != 0 ? status : -84);
    uint32_t gcap = 0U;
    status = register_read(driver, HDA_GCAP, 2U, &gcap);
    if (status != 0)
        return driver_failure(driver, HDA_DIAGNOSTIC_STAGE_GCAP, status);
    uint32_t input_streams = (gcap >> 8U) & 0x0FU;
    uint32_t output_streams = (gcap >> 12U) & 0x0FU;
    if (output_streams == 0U || input_streams + output_streams > 30U)
        return driver_failure(driver, HDA_DIAGNOSTIC_STAGE_GCAP, -95);
    driver->stream_index = input_streams;
    driver->stream_base = HDA_STREAM_BASE +
        driver->stream_index * HDA_STREAM_STRIDE;
    status = controller_reset(driver);
    if (status != 0)
        return driver_failure(
            driver, HDA_DIAGNOSTIC_STAGE_CONTROLLER_RESET, status);
    status = codec_discover(driver);
    if (status != 0)
        return driver_failure(
            driver, HDA_DIAGNOSTIC_STAGE_CODEC_DISCOVERY, status);
    status = x86os_ipc_create(&driver->control);
    if (status != 0)
        return driver_failure(
            driver, HDA_DIAGNOSTIC_STAGE_CONTROL_CHANNEL, status);
    status = x86os_device_driver_report(
        &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_CHANNEL,
        driver->control);
    if (status != 0)
        return driver_failure(
            driver, HDA_DIAGNOSTIC_STAGE_CHANNEL_REPORT, status);
    status = x86os_device_driver_report(
        &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_SELF_TEST, 1U);
    if (status != 0)
        return driver_failure(driver, HDA_DIAGNOSTIC_STAGE_SELF_TEST, status);
    status = x86os_device_driver_report(
        &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_PROGRESS, 1U);
    if (status != 0)
        return driver_failure(driver, HDA_DIAGNOSTIC_STAGE_PROGRESS, status);
    driver->progress = 2U;
    return 0;
}

#ifndef REIST_HDA_DRIVER_NO_MAIN
int main(void) {
    hda_driver_t driver;
    if (driver_initialize(&driver) != 0) return 1;
    uint64_t last_report = 0U;
    (void)x86os_monotonic_ms(&last_report);
    for (;;) {
        service_control_poll(&driver);
        if (driver.fatal != 0U) return 3;
        irq_poll(&driver);
        uint64_t now = 0U;
        if (x86os_monotonic_ms(&now) == 0 &&
            now - last_report >= HDA_HEARTBEAT_MS) {
            if (driver.progress == 0U) driver.progress = 1U;
            if (x86os_device_driver_report(
                    &driver.bootstrap, X86OS_DEVICE_DRIVER_REPORT_PROGRESS,
                    driver.progress++) != 0) return 2;
            last_report = now;
        }
    }
}
#endif
#endif
