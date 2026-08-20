/** @file hda_driver.h @brief Bounded Intel HDA Ring-3 driver contract. */
#ifndef REIST_HDA_DRIVER_H
#define REIST_HDA_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#define REIST_HDA_STREAM_FORMAT_S16_STEREO_48K 0x0011U

/** Decode the standard HDA subordinate-node parameter response. */
bool reist_hda_decode_nodes(uint32_t response, uint8_t *start_node,
                            uint8_t *node_count);
/** Extract the valid 0-dB gain step from an HDA amp-capability response. */
bool reist_hda_amp_0db_gain(uint32_t capabilities, uint8_t *gain);

#endif
