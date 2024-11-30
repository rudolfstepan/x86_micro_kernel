#include "drivers/rtc/rtc.h"
#include "toolchain/stdio.h"
#include "drivers/video/video.h"

#define SCREEN_WIDTH 80  // Assuming 80x25 screen
#define TIME_ROW 0       // Row to display the time
#define TIME_COL (SCREEN_WIDTH - 8) // 8 characters for HH:MM:SS

int main() {
    printf("Current date/time:\n");

    // Store the initial cursor position
    int initial_x, initial_y;
    

    while (1) {
        get_cursor_position(&initial_x, &initial_y);
        // Read date and time
        int year, month, day, hour, minute, second;
        read_date(&year, &month, &day);
        read_time(&hour, &minute, &second);

        // Format the time as a string
        char time_str[9]; // HH:MM:SS\0
        snprintf(time_str, sizeof(time_str), "%d:%d:%d", hour, minute, second);

        // Display the time in the top-right corner
        set_cursor_position(TIME_COL, TIME_ROW);
        printf("%s", time_str);

        // Restore the original cursor position
        set_cursor_position(initial_x, initial_y);

        // Small delay to reduce flicker
        asm volatile("int $0x29"); // Trigger a timer interrupt
    }

    return 0;
}
