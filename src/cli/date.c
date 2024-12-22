#include "drivers/rtc/rtc.h"
#include "toolchain/stdio.h"
#include "drivers/video/framebuffer.h"
// #include "drivers/video/vga.h"

#define SCREEN_WIDTH 80  // Assuming 80x25 screen
#define TIME_ROW 0       // Row to display the time
#define TIME_COL (SCREEN_WIDTH - 8) // 8 characters for HH:MM:SS

int main() {
    printf("Current date/time:\n");

    // Store the initial cursor position
    uint32_t initial_x = 0;
    uint32_t initial_y = 0;

    // Variables to store previous and current time
    int prev_hour = -1, prev_minute = -1, prev_second = -1;
    int year, month, day, hour, minute, second;

    //while (1) {
        //fb_get_cursor_position(&initial_x, &initial_y);

        // printf("Cursor position x: %u ", initial_x);
        // printf(" y: %u\n", initial_y);

        // Read the current date and time
        read_date(&year, &month, &day);
        read_time(&hour, &minute, &second);

        // Check if the time has changed
        if (hour != prev_hour || minute != prev_minute || second != prev_second) {
            // Update the previous time
            prev_hour = hour;
            prev_minute = minute;
            prev_second = second;

            // Format the time as a string
            char time_str[9]; // HH:MM:SS\0
            snprintf(time_str, sizeof(time_str), "%d:%d:%d", hour, minute, second);

            // Display the time in the top-right corner
            //fb_set_cursor_position(TIME_COL, TIME_ROW);
            printf("%s\n", time_str);

            // Restore the original cursor position
            //fb_set_cursor_position(initial_x, initial_y);
        }

        // Trigger a timer interrupt (non-blocking, allows context switching)
        asm volatile("int $0x29");
    //}

    return 0;
}
