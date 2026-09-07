/* Frozen R3.20 work: same C caller, no clock/allocator inside either loop. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "browser_model.h"
int main(void) {
    char text[256] = "https://example.test/";
    const uint32_t initial_length = sizeof("https://example.test/") - 1U;
    uint32_t length = initial_length, cursor = initial_length, replace = 0U;
    LARGE_INTEGER frequency, begin, end;
    assert(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0);
    assert(QueryPerformanceCounter(&begin));
    for (unsigned i = 0; i < 200000; ++i) {
        assert(browser_address_edit(text, sizeof(text), &length, &cursor, &replace, 'x') == 1);
        assert(length == initial_length+1 && cursor == length && !replace && text[initial_length] == 'x' && !text[length]);
        assert(browser_address_edit(text, sizeof(text), &length, &cursor, &replace, 8) == 1);
        assert(length == initial_length && cursor == length && !replace && !text[length]);
    }
    assert(QueryPerformanceCounter(&end) && end.QuadPart > begin.QuadPart);
    double address = (double)(end.QuadPart-begin.QuadPart)*1e9/frequency.QuadPart/200000.0;
    browser_scrollbar_t bar = {0};
    browser_scrollbar_configure(&bar, 800, 502, 1800, 337);
    assert(QueryPerformanceCounter(&begin));
    for (unsigned i = 0; i < 200000; ++i) {
        int32_t x = bar.thumb.x + 4, y = bar.thumb.y + 11;
        assert(browser_scrollbar_pointer(&bar, 0, 1, x, y) == 1);
        assert(bar.state.value == 337 && bar.state.captured && bar.state.focused);
        assert(browser_scrollbar_pointer(&bar, 1, 1, x, y + 20) == 1);
        int32_t moved = 337 + 20 * bar.model.maximum / (int32_t)(bar.track.height-bar.thumb.height);
        assert(bar.state.value == moved && bar.state.captured);
        assert(browser_scrollbar_pointer(&bar, 0, 0, x, y + 20) == 1);
        assert(bar.state.value == moved && !bar.state.captured);
        browser_scrollbar_configure(&bar, 800, 502, 1800, 337);
        assert(bar.state.value == 337 && !bar.state.captured && bar.state.focused);
    }
    assert(QueryPerformanceCounter(&end) && end.QuadPart > begin.QuadPart);
    printf("{\"address_ns\":%.3f,\"scrollbar_ns\":%.3f}\n", address,
        (double)(end.QuadPart-begin.QuadPart)*1e9/frequency.QuadPart/200000.0);
    return 0;
}
