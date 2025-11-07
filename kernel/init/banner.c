/**
 * @file banner.c
 * @brief Kernel welcome messages and visual output
 * 
 * Contains functions for displaying the kernel's welcome banner,
 * color tests, and command prompt.
 */

#include "lib/libc/stdio.h"
#include "drivers/video/display.h"
#include "drivers/bus/drives.h"
#include "mm/kmalloc.h"

// External global variables
extern size_t total_memory;         // From kmalloc.h
extern short drive_count;            // From drives.h
extern drive_t detected_drives[];    // From drives.h
extern drive_t* current_drive;       // From drives.h

/**
 * Display the kernel's main welcome banner
 * Shows system information including memory and detected drives
 */
void print_welcome_message(void) {
    display_set_color(WHITE);
    printf("\n");
    printf("      *------------------------------------------------------------*\n");
    printf("      |        Welcome to the Rudolf Stepan x86 Micro Kernel       |\n");
    printf("      |      Type 'HELP' for a list of commands and instructions   |\n");
    printf("      *------------------------------------------------------------*\n");
    printf("        Total Memory: %d MB\n", total_memory/1024/1024);
    printf("        Detected Drives (%d): ", drive_count);
    
    for(int i = 0; i < drive_count; i++) {
        const char* drive_type = (detected_drives[i].type == DRIVE_TYPE_ATA) ? "ATA" : "FDD";
        printf(" %s: %s ", drive_type, detected_drives[i].name);
    }

    printf("\n\n    Enter a Command or help for a complete list of supported commands.\n");
    display_set_color(WHITE);
}

/**
 * Display all available VGA text colors for testing
 * Useful for verifying display driver functionality
 */
void display_color_test(void) {
    printf("\nColor Test: ");
    display_set_color(BLACK); printf("Black ");
    display_set_color(BLUE); printf("Blue ");
    display_set_color(GREEN); printf("Green ");
    display_set_color(CYAN); printf("Cyan ");
    display_set_color(RED); printf("Red ");
    display_set_color(MAGENTA); printf("Magenta ");
    display_set_color(BROWN); printf("Brown ");
    display_set_color(LIGHT_GRAY); printf("Light Grey ");
    display_set_color(DARK_GRAY); printf("Dark Grey ");
    display_set_color(LIGHT_BLUE); printf("Light Blue ");
    display_set_color(LIGHT_GREEN); printf("Light Green ");
    display_set_color(LIGHT_CYAN); printf("Light Cyan ");
    display_set_color(LIGHT_RED); printf("Light Red ");
    display_set_color(LIGHT_MAGENTA); printf("Light Magenta ");
    display_set_color(YELLOW); printf("Yellow ");
    display_set_color(WHITE); printf("White\n\n");
    display_set_color(WHITE);
}

/**
 * Print the shell command prompt
 * Currently shows a simple '>' prompt in green
 */
void print_fancy_prompt(void) {
    display_set_color(GREEN);

    // Check if a drive is mounted
    if (current_drive == NULL) {
        printf(">");
        display_set_color(WHITE);
        return;
    }

    // Could enhance this to show current drive/path
    printf(">");
    display_set_color(WHITE);
}
