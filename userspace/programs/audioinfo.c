/** @file audioinfo.c @brief Report the supervised REIST PCM backend. */
#include <stdint.h>

#include "reist/audio.h"

static void put_u32(uint32_t value) {
    char digits[10];
    uint32_t count = 0U;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U) x86os_putchar(digits[--count]);
}

static void put_result(const char *prefix, int result) {
    x86os_puts(prefix);
    if (result < 0) {
        x86os_putchar('-');
        put_u32((uint32_t)(-(int64_t)result));
    } else {
        put_u32((uint32_t)result);
    }
    x86os_putchar('\n');
}

int main(void) {
    reist_audio_context_t audio;
    int result = reist_audio_init(&audio);
    if (result != 0) {
        put_result("Audio service unavailable: ", result);
        return 1;
    }
    reist_audio_info_t info;
    result = reist_audio_get_info(&audio, &info);
    if (result != 0) {
        put_result("Audio query failed: ", result);
        reist_audio_shutdown(&audio);
        return 2;
    }
    x86os_puts("REIST audio: ready\n");
    x86os_puts("Backend    : Intel HDA Ring 3\n");
    x86os_puts("Format     : S16_LE interleaved\n");
    x86os_puts("Rate       : ");
    put_u32(info.preferred_format.sample_rate);
    x86os_puts(" Hz\nChannels   : ");
    put_u32(info.preferred_format.channels);
    x86os_puts("\nMax frames : ");
    put_u32(info.max_stream_frames);
    x86os_putchar('\n');
    reist_audio_shutdown(&audio);
    return 0;
}
