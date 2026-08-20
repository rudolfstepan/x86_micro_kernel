/** @file test_audio_host.c @brief Host checks for pure HDA decoding rules. */
#include <assert.h>
#include <stdint.h>

#include "hda_driver.h"

int main(void) {
    uint8_t start = 0U;
    uint8_t count = 0U;
    assert(reist_hda_decode_nodes(0x00020002U, &start, &count));
    assert(start == 2U && count == 2U);
    assert(!reist_hda_decode_nodes(0U, &start, &count));
    assert(!reist_hda_decode_nodes(0x00FF0002U, &start, &count));
    assert(!reist_hda_decode_nodes(0x00020002U, 0, &count));

    uint8_t gain = 0U;
    /* QEMU HDA: Offset 74 is the codec-declared 0-dB step. */
    assert(reist_hda_amp_0db_gain(0x80034A4AU, &gain));
    assert(gain == 0x4AU);
    assert(reist_hda_amp_0db_gain(0U, &gain));
    assert(gain == 0U);
    assert(!reist_hda_amp_0db_gain(0x00000102U, &gain));
    assert(!reist_hda_amp_0db_gain(0x00000101U, 0));
    return 0;
}
