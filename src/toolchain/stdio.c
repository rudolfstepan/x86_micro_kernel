#include "stdio.h"
#include "strings.h"
#include "stdlib.h"

#include <stdarg.h>
#include <stddef.h>


#include "filesystem/filesystem.h"
#include "filesystem/fat32/fat32.h"
#include "filesystem/fat12/fat12.h"
#include "drivers/io/io.h"
#include "drivers/video/video.h"
#include "drivers/kb/kb.h"


// for memory dump
#define BYTES_PER_LINE 16
#define MAX_LINES 20

// PIT I/O port addresses
#define PIT_CONTROL_PORT 0x43
#define PIT_CHANNEL_2_PORT 0x42
#define PC_SPEAKER_PORT 0x61

static inline int is_kernel_context() {
    unsigned short cs;
    asm volatile ("mov %%cs, %0" : "=r" (cs));
    return (cs & 3) == 0; // CPL (Current Privilege Level) 0 means kernel mode
}

void* syscall(int syscall_index, void* parameter1, void* parameter2, void* parameter3) {
    void* return_value;
    __asm__ volatile(
        "int $0x80\n"       // Trigger syscall interrupt
        : "=a"(return_value) // Output: Get return value from EAX
        : "a"(syscall_index), "b"(parameter1), "c"(parameter2), "d"(parameter3) // Inputs
        : "memory"          // Clobbers
    );
    return return_value;     // Return the value in EAX
}

// -----------------------------------------------------------------
// Directory Handling Functions
// the following functions are defined in the filesystem/fat32/fat32.c file
// -----------------------------------------------------------------
int mkdir(const char* path, uint8_t mode) {
    if(is_kernel_context()) {
        return fat32_create_dir(path);
    }
    return -1;
}

int rmdir(const char* path) {
    if(is_kernel_context()) {
        return fat32_delete_dir(path);
    }
    return -1;
}

// struct dirent* readdir(DIR* dirp) {
//     return fat32_read_directory(dirp);
// }

int readdir(const char* path, char* buffer, unsigned int* size, uint8_t dt) {
    if(is_kernel_context()) {
        drive_type_t driveType = (drive_type_t)dt;
        
        if (driveType == DRIVE_TYPE_NONE) {
            printf("Invalid drive type\n");
            return -1;
        }
        if (driveType == DRIVE_TYPE_ATA) {
            return fat32_read_dir(path);
        }
        if (driveType == DRIVE_TYPE_FDD) {
            return fat12_read_dir(path);
        }
    }

    return -1;
}

// -----------------------------------------------------------------
// File Functions
// -----------------------------------------------------------------

FILE* fopen(const char* filename, const char* mode) {
    if(is_kernel_context()) {
        return fat32_open_file(filename, mode);
    }
    return NULL;
}

size_t fread(void* buffer, size_t size, size_t count, FILE* stream) {
    if(is_kernel_context()) {
        return fat32_read_file(stream, buffer, size, count);
    }
    return 0;
}

int remove(const char* path) {
    if(is_kernel_context()) {
        return fat32_delete_file(path);
    }
    return -1;
}

int mkfile(const char* path) {
    if(is_kernel_context()) {
        return fat32_create_file(path);
    }
    return -1;
}

// -----------------------------------------------------------------
// Console Functions
// -----------------------------------------------------------------

// Helper function to reverse a string
static void reverse(char* str, int length) {
    int start = 0;
    int end = length - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

// Helper function to convert an integer to a string
static int int_to_str(int num, char* str, int base) {
    int i = 0;
    bool is_negative = false;

    // Handle 0 explicitly
    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return i;
    }

    // Handle negative numbers for decimal base
    if (num < 0 && base == 10) {
        is_negative = true;
        num = -num;
    }

    // Process individual digits
    while (num != 0) {
        int rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    }

    // Append '-' if the number is negative
    if (is_negative) {
        str[i++] = '-';
    }

    str[i] = '\0';  // Null-terminate the string

    // Reverse the string
    reverse(str, i);
    return i;
}

void unsigned_int_to_str(unsigned int value, char* buffer, int base) {
    int i = 0;
    if (value == 0) {
        buffer[i++] = '0';
        buffer[i] = '\0';
        return;
    }

    while (value > 0) {
        int digit = value % base;
        buffer[i++] = (digit > 9) ? (digit - 10) + 'a' : digit + '0';
        value /= base;
    }

    buffer[i] = '\0';

    // Reverse the buffer
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = buffer[start];
        buffer[start] = buffer[end];
        buffer[end] = temp;
        start++;
        end--;
    }
}

void int_to_str2(int value, char* str, int base) {
    char* digits = "0123456789ABCDEF";
    char temp[32];
    int i = 0;
    int isNegative = 0;

    if (value == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }

    if (value < 0 && base == 10) {
        isNegative = 1;
        value = -value;
    }

    while (value != 0) {
        temp[i++] = digits[value % base];
        value /= base;
    }

    if (isNegative) {
        temp[i++] = '-';
    }

    temp[i] = '\0';

    // Reverse the string
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char t = temp[start];
        temp[start] = temp[end];
        temp[end] = t;
        start++;
        end--;
    }

    i = 0;
    while (temp[i] != '\0') {
        str[i] = temp[i];
        i++;
    }
    str[i] = '\0';
}

void putchar(char c) {
    if (is_kernel_context()) {
        vga_write_char(c);
    } else {
         syscall(SYS_TERMINAL_PUTCHAR, (void*)(uintptr_t)c, NULL, NULL);
    }
}

// Function to print an unsigned integer
void print_unsigned(unsigned int value, unsigned int base) {
    char buffer[32]; // Enough for base-2 representation of 32-bit integer
    int i = 30;

    // Handle 0 explicitly, otherwise it will be printed as an empty string
    if (value == 0) {
        syscall(SYS_TERMINAL_PUTCHAR, (void*)'0', NULL, NULL);
        return;
    }

    for (; value && i; --i, value /= base) {
        buffer[i] = "0123456789abcdef"[value % base];
    }

    for (i++; i < 31; i++) {
        syscall(SYS_TERMINAL_PUTCHAR, (void*)(uintptr_t)buffer[i], NULL, NULL);
    }
}

void print_hex(unsigned int value) {
    char hexString[9]; // 8 characters for 32-bit address + 1 for null-terminator
    char* hexChars = "0123456789ABCDEF";
    hexString[8] = '\0';

    for (int i = 7; i >= 0; i--) {
        hexString[i] = hexChars[value & 0xF];
        value >>= 4;
    }

    char prefix[] = "0x";
    char* s = prefix;
    while (*s) {
        putchar(*s++);
    }

    s = hexString;
    while (*s) {
        putchar(*s++);
    }
}

void print_hex_padded(unsigned int value, int width) {
    char hex_buffer[33]; // Enough for 32 digits plus a null terminator
    char* ptr = &hex_buffer[32];
    *ptr = '\0';

    do {
        int hex_digit = value & 0xF;
        *--ptr = (hex_digit < 10) ? (hex_digit + '0') : (hex_digit - 10 + 'A');
        value >>= 4;
    } while (value != 0);

    int num_digits = &hex_buffer[32] - ptr;
    for (int i = 0; i < width - num_digits; ++i) {
        putchar('0');
    }

    while (*ptr) {
        putchar(*ptr++);
    }
}

void print_hex64(uint64_t value) {
    char buffer[16]; // 64 bits = 16 hex digits
    const char* hex_digits = "0123456789ABCDEF";
    for (int i = 15; i >= 0; i--) {
        buffer[i] = hex_digits[value & 0xF];
        value >>= 4;
    }
    for (int i = 0; i < 16; i++) {
        putchar(buffer[i]);
    }
}

/*
    * printf() implementation
    * Supports %c, %s, %d, %u, %p, %X format specifiers
    * Supports width, precision, and padding
*/
int printf(const char* format, ...) {
    va_list args;
    va_start(args, format);

    while (*format != '\0') {
        if (*format == '%') {
            format++;

            // Parse width, alignment, and padding
            int width = 0;
            bool width_specified = false;
            bool zero_padding = false;
            bool left_align = false;
            int precision = -1;  // No precision by default

            // Check for left alignment flag (e.g., %-8s)
            if (*format == '-') {
                left_align = true;
                format++;
            }

            // Check for zero padding (e.g., %08d) if not left-aligned
            if (*format == '0' && !left_align) {
                zero_padding = true;
                format++;
            }

            // Parse width: either a numeric value or '*' for dynamic width
            if (*format == '*') {
                width = va_arg(args, int);
                width_specified = true;
                format++;
            } else {
                while (*format >= '0' && *format <= '9') {
                    width = width * 10 + (*format - '0');
                    width_specified = true;
                    format++;
                }
            }

            // Parse precision if present (e.g., %.11s)
            if (*format == '.') {
                format++;
                precision = 0;
                while (*format >= '0' && *format <= '9') {
                    precision = precision * 10 + (*format - '0');
                    format++;
                }
            }

            if (strncmp(format, "llX", 3) == 0) {
                uint64_t llx = va_arg(args, uint64_t);
                print_hex64(llx);  // This assumes print_hex64 handles width.
                format += 2;       // Skip past 'llX'
            } else {
                switch (*format) {
                case 'c': {
                    char c = (char)va_arg(args, int);
                    putchar(c);
                    break;
                }
                case 's': {
                    char* s = va_arg(args, char*);
                    int len = strlen(s);

                    // Apply precision limit if specified
                    if (precision >= 0 && precision < len) {
                        len = precision;
                    }

                    // Calculate padding
                    int pad = (width > len) ? width - len : 0;

                    if (left_align) {
                        // Print the string first, then padding
                        for (int i = 0; i < len; i++) {
                            putchar(s[i]);
                        }
                        for (int i = 0; i < pad; i++) {
                            putchar(' ');
                        }
                    } else {
                        // Print padding first, then the string
                        for (int i = 0; i < pad; i++) {
                            putchar(zero_padding ? '0' : ' ');
                        }
                        for (int i = 0; i < len; i++) {
                            putchar(s[i]);
                        }
                    }
                    break;
                }
                case 'u': {
                    unsigned int u = va_arg(args, unsigned int);
                    char buffer[32];
                    unsigned_int_to_str(u, buffer, 10);
                    int len = strlen(buffer);
                    int pad = width - len;
                    if (width_specified && pad > 0) {
                        for (int i = 0; i < pad; i++) putchar(zero_padding ? '0' : ' ');
                    }
                    char* s = buffer;
                    while (*s) {
                        putchar(*s++);
                    }
                    break;
                }
                case 'd': {
                    int i = va_arg(args, int);
                    if (i < 0) {
                        putchar('-');
                        i = -i;
                    }

                    char buffer[32];
                    int_to_str(i, buffer, 10);
                    int len = strlen(buffer);
                    int pad = width - len;
                    if (width_specified && pad > 0) {
                        for (int i = 0; i < pad; i++) putchar(zero_padding ? '0' : ' ');
                    }
                    char* s = buffer;
                    while (*s) {
                        putchar(*s++);
                    }
                    break;
                }
                case 'p': {
                    void* p = va_arg(args, void*);
                    print_hex((unsigned int)p); // Assuming 32-bit addresses
                    break;
                }
                case 'X': {
                    unsigned int x = va_arg(args, unsigned int);
                    char buffer[32];
                    int_to_hex_str(x, buffer, width, zero_padding); // Adjusted function to handle padding
                    char* s = buffer;
                    while (*s) {
                        putchar(*s++);
                    }
                    break;
                }
                }
            }
        } else {
            putchar(*format);
        }
        format++;
    }

    va_end(args);
    return 0;
}

// Main sprintf implementation
int sprintf(char* buffer, const char* format, ...) {
    va_list args;
    va_start(args, format);

    char* str = buffer;
    const char* p = format;
    char temp[20];  // Temporary buffer for integer conversion
    int written = 0;

    while (*p != '\0') {
        if (*p == '%') {
            p++;  // Move past '%'

            if (*p == 's') {  // String
                char* arg = va_arg(args, char*);
                while (*arg != '\0') {
                    *str++ = *arg++;
                    written++;
                }
            } else if (*p == 'd') {  // Signed integer
                int arg = va_arg(args, int);
                int len = int_to_str(arg, temp, 10);
                for (int i = 0; i < len; i++) {
                    *str++ = temp[i];
                    written++;
                }
            } else if (*p == 'x') {  // Hexadecimal
                int arg = va_arg(args, int);
                int len = int_to_str(arg, temp, 16);
                for (int i = 0; i < len; i++) {
                    *str++ = temp[i];
                    written++;
                }
            } else {
                // Unsupported format specifier; copy as-is
                *str++ = '%';
                *str++ = *p;
                written += 2;
            }
        } else {
            // Regular character, copy as-is
            *str++ = *p;
            written++;
        }
        p++;
    }

    *str = '\0';  // Null-terminate the resulting string

    va_end(args);
    return written;
}

// Simple implementation of snprintf
int snprintf(char* str, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);

    unsigned int written = 0;
    const char* p = format;

    while (*p != '\0' && written < size) {
        if (*p == '%') {
            p++;  // Skip '%'
            if (*p == 's') {  // String format
                const char* arg = va_arg(args, const char*);
                while (*arg != '\0' && written < size) {
                    str[written++] = *arg++;
                }
            } else if (*p == 'd') {  // Integer format
                int num = va_arg(args, int);
                // Convert integer to string
                char num_buffer[12];  // Buffer to hold the integer as string (supports up to 32-bit integer range)
                int num_written = 0;

                if (num < 0) {  // Handle negative numbers
                    if (written < size) {
                        str[written++] = '-';
                    }
                    num = -num;
                }

                int temp = num;
                do {
                    num_buffer[num_written++] = (temp % 10) + '0';
                    temp /= 10;
                } while (temp > 0 && num_written < (int)sizeof(num_buffer));

                // Reverse the number buffer into the main string buffer
                for (int i = num_written - 1; i >= 0 && written < size; i--) {
                    str[written++] = num_buffer[i];
                }
            }
            // Add other format specifiers as needed
        } else {
            str[written++] = *p;
        }
        p++;
    }

    va_end(args);

    // Null-terminate the string
    if (size > 0) {
        if (written < size) {
            str[written] = '\0';
        } else {
            str[size - 1] = '\0';
        }
    }

    return written;
}

void hex_dump(const unsigned char* data, size_t size) {
    int line_count = 0;  // Counter to track lines printed

    for (size_t i = 0; i < size; i += 16) {
        // Print offset
        printf("%08X  ", (unsigned int)i);

        // Print hex values
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                printf("%02X ", data[i + j]);
            } else {
                printf("   ");
            }
        }

        // Print ASCII characters
        printf(" ");
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                unsigned char c = data[i + j];
                printf("%c", (c >= 32 && c <= 126) ? c : '.');
            }
        }
        printf("\n");

        // Increment line count and check if we've printed 20 lines
        line_count++;
        if (line_count >= 20) {
            // Reset the line count
            line_count = 0;
            wait_enter_pressed(); // Wait for key press
        }
    }
}

// Check if a character is printable
int is_printable(char ch) {
    return (ch >= 32 && ch < 127);
}

// Convert a byte to a printable character or '.'
char to_printable_char(char ch) {
    return is_printable(ch) ? ch : '.';
}

// Memory dump function
void memory_dump(uint32_t start_address, uint32_t end_address) {
    if (end_address == 0) {
        end_address = start_address + (BYTES_PER_LINE * MAX_LINES); // Default length for 20 lines
    }

    uint8_t* ptr = (uint8_t*)start_address;
    int line_count = 0;

    while (ptr < (uint8_t*)end_address) {
        printf("%08X: ", (unsigned int)(uintptr_t)ptr);

        // Print each byte in hex and store ASCII characters
        char ascii[BYTES_PER_LINE + 1];

        for (int i = 0; i < BYTES_PER_LINE; ++i) {
            if (ptr + i < (uint8_t*)end_address) {
                printf("%02X ", ptr[i]);
                ascii[i] = is_printable(ptr[i]) ? ptr[i] : '.';
            } else {
                printf("   ");
                ascii[i] = ' ';
            }
        }

        printf(" |%s|\n", ascii);
        ptr += BYTES_PER_LINE;
        line_count++;

        if (line_count >= MAX_LINES) {
            wait_enter_pressed(); // Wait for user to press Enter
            line_count = 0;   // Reset line count
        }
    }
}

// Set the PIT to the desired frequency for the beep
void set_pit_frequency(uint32_t frequency) {
#ifdef __kernel__

    uint32_t divisor = 1193180 / frequency; // Calculate the divisor (PIT runs at ~1.19318 MHz)
    
    // Send command byte to PIT control port (select channel 2, mode 3, binary mode)
    outb(PIT_CONTROL_PORT, 0xB6);
    
    // Send low byte of divisor
    outb(PIT_CHANNEL_2_PORT, (uint8_t)(divisor & 0xFF));
    
    // Send high byte of divisor
    outb(PIT_CHANNEL_2_PORT, (uint8_t)((divisor >> 8) & 0xFF));
#endif

}

// Enable the PC speaker
void enable_pc_speaker() {
    #ifdef __kernel__
    uint8_t tmp = inb(PC_SPEAKER_PORT);
    if (!(tmp & 0x03)) { // Check if the speaker is already enabled
        outb(PC_SPEAKER_PORT, tmp | 0x03); // Turn on the speaker
    }
    #endif
}

// Disable the PC speaker
void disable_pc_speaker() {
    #ifdef __kernel__
    uint8_t tmp = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, tmp & 0xFC); // Turn off the speaker
    #endif
}

// Function to create a beep sound
void beep(uint32_t frequency, uint32_t duration_ms) {
    #ifdef __kernel__
    set_pit_frequency(frequency);
    enable_pc_speaker();

    //printf("Beep at %u Hz for %u ms\n", frequency, duration_ms);
    
    // Simple delay loop for the beep duration
    sleep_ms(duration_ms);
    
    disable_pc_speaker();

    #endif
}
