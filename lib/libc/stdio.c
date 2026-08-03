#include "stdio.h"
#include "string.h"
#include "stdlib.h"

#include <stdarg.h>
#include <stddef.h>


#include "../../fs/vfs/filesystem.h"
#include "../../drivers/bus/drives.h"
#include "../../fs/fat32/fat32.h"
#include "../../fs/fat12/fat12.h"
#include "drivers/char/io.h"
#include "drivers/video/display.h"
#include "drivers/char/kb.h"


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
        : "memory", "cc"    // Clobbers
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

int isprint(int c) {
    // Check if c is a printable ASCII character (32 to 126)
    return (c >= 32 && c <= 126);
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
    const char* digits = "0123456789ABCDEF";
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
        display_putchar(c);
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
    const char* hexChars = "0123456789ABCDEF";
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

void uint64_t_to_str(uint64_t value, char* buffer, int base) {
    if (base < 2 || base > 16) {
        // Unsupported base
        buffer[0] = '\0';
        return;
    }

    char temp[64]; // Temporary buffer for the conversion
    int i = 0;

    // Handle zero explicitly
    if (value == 0) {
        temp[i++] = '0';
    } else {
        // Convert value to the specified base
        while (value > 0) {
            temp[i++] = "0123456789ABCDEF"[value % base];
            value /= base;
        }
    }

    // Reverse the string into the output buffer
    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';
}

/*
    * printf() implementation
    * Supports %c, %s, %d, %u, %p, %X format specifiers
    * Supports width, precision, and padding
*/
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Erwartet vorhandene Helfer:
// putchar, print_hex64, uint64_t_to_str, unsigned_int_to_str, int_to_str,
// int_to_hex_str (deine Varianten)

typedef enum {
    FORMAT_LENGTH_DEFAULT,
    FORMAT_LENGTH_LONG,
    FORMAT_LENGTH_LONG_LONG,
    FORMAT_LENGTH_SIZE
} format_length_t;

typedef struct {
    char *buffer;
    size_t capacity;
    size_t count;
    bool console;
} format_output_t;

static void format_emit(format_output_t *output, char value) {
    if (output->console) {
        putchar(value);
    } else if (output->buffer != NULL && output->capacity > 0 &&
               output->count < output->capacity - 1U) {
        output->buffer[output->count] = value;
    }
    if (output->count != SIZE_MAX) {
        output->count++;
    }
}

static void format_repeat(format_output_t *output, char value, int count) {
    while (count-- > 0) {
        format_emit(output, value);
    }
}

static size_t format_unsigned_value(unsigned long long value, unsigned int base,
                                    bool uppercase, char buffer[65]) {
    const char *digits = uppercase ? "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                   : "0123456789abcdefghijklmnopqrstuvwxyz";
    char reverse[65];
    size_t length = 0;
    do {
        reverse[length++] = digits[value % base];
        value /= base;
    } while (value != 0);

    for (size_t i = 0; i < length; ++i) {
        buffer[i] = reverse[length - i - 1U];
    }
    buffer[length] = '\0';
    return length;
}

static void format_number(format_output_t *output, unsigned long long value,
                          bool negative, unsigned int base, bool uppercase,
                          int width, int precision, bool left_align,
                          bool zero_padding) {
    char digits[65];
    size_t digit_count = format_unsigned_value(value, base, uppercase, digits);
    if (precision == 0 && value == 0) {
        digit_count = 0;
    }

    int leading_zeroes = 0;
    if (precision > (int)digit_count) {
        leading_zeroes = precision - (int)digit_count;
    }
    int content_width = (negative ? 1 : 0) + leading_zeroes + (int)digit_count;
    int padding = width > content_width ? width - content_width : 0;

    if (!left_align && zero_padding && precision < 0) {
        leading_zeroes += padding;
        padding = 0;
    }
    if (!left_align) {
        format_repeat(output, ' ', padding);
    }
    if (negative) {
        format_emit(output, '-');
    }
    format_repeat(output, '0', leading_zeroes);
    for (size_t i = 0; i < digit_count; ++i) {
        format_emit(output, digits[i]);
    }
    if (left_align) {
        format_repeat(output, ' ', padding);
    }
}

static int format_v(format_output_t *output, const char *format, va_list args) {
    if (format == NULL) {
        return -1;
    }

    while (*format != '\0') {
        if (*format != '%') {
            format_emit(output, *format++);
            continue;
        }
        format++;

        bool left_align = false;
        bool zero_padding = false;
        bool parsing_flags = true;
        while (parsing_flags) {
            switch (*format) {
                case '-': left_align = true; format++; break;
                case '0': zero_padding = true; format++; break;
                default: parsing_flags = false; break;
            }
        }

        int width = 0;
        if (*format == '*') {
            width = va_arg(args, int);
            format++;
            if (width < 0) {
                left_align = true;
                width = width == INT_MIN ? INT_MAX : -width;
            }
        } else {
            while (*format >= '0' && *format <= '9') {
                int digit = *format - '0';
                width = width > (INT_MAX - digit) / 10
                    ? INT_MAX : width * 10 + digit;
                format++;
            }
        }

        int precision = -1;
        if (*format == '.') {
            format++;
            precision = 0;
            if (*format == '*') {
                precision = va_arg(args, int);
                format++;
                if (precision < 0) precision = -1;
            } else {
                while (*format >= '0' && *format <= '9') {
                    int digit = *format - '0';
                    precision = precision > (INT_MAX - digit) / 10
                        ? INT_MAX : precision * 10 + digit;
                    format++;
                }
            }
        }

        format_length_t length = FORMAT_LENGTH_DEFAULT;
        if (*format == 'l') {
            format++;
            length = FORMAT_LENGTH_LONG;
            if (*format == 'l') {
                format++;
                length = FORMAT_LENGTH_LONG_LONG;
            }
        } else if (*format == 'z') {
            format++;
            length = FORMAT_LENGTH_SIZE;
        }

        char specifier = *format;
        if (specifier == '\0') {
            format_emit(output, '%');
            break;
        }
        format++;

        if (specifier == '%') {
            format_emit(output, '%');
        } else if (specifier == 'c') {
            int padding = width > 1 ? width - 1 : 0;
            if (!left_align) format_repeat(output, ' ', padding);
            format_emit(output, (char)va_arg(args, int));
            if (left_align) format_repeat(output, ' ', padding);
        } else if (specifier == 's') {
            const char *value = va_arg(args, const char*);
            if (value == NULL) value = "(null)";
            size_t length_value = strlen(value);
            if (precision >= 0 && length_value > (size_t)precision) {
                length_value = (size_t)precision;
            }
            int padding = width > (int)length_value ? width - (int)length_value : 0;
            if (!left_align) format_repeat(output, ' ', padding);
            for (size_t i = 0; i < length_value; ++i) format_emit(output, value[i]);
            if (left_align) format_repeat(output, ' ', padding);
        } else if (specifier == 'd' || specifier == 'i') {
            long long value;
            if (length == FORMAT_LENGTH_LONG_LONG) value = va_arg(args, long long);
            else if (length == FORMAT_LENGTH_LONG) value = va_arg(args, long);
            else if (length == FORMAT_LENGTH_SIZE) value = va_arg(args, ptrdiff_t);
            else value = va_arg(args, int);

            bool negative = value < 0;
            unsigned long long magnitude = negative
                ? 0ULL - (unsigned long long)value
                : (unsigned long long)value;
            format_number(output, magnitude, negative, 10, false, width,
                          precision, left_align, zero_padding);
        } else if (specifier == 'u' || specifier == 'x' ||
                   specifier == 'X' || specifier == 'o') {
            unsigned long long value;
            if (length == FORMAT_LENGTH_LONG_LONG)
                value = va_arg(args, unsigned long long);
            else if (length == FORMAT_LENGTH_LONG)
                value = va_arg(args, unsigned long);
            else if (length == FORMAT_LENGTH_SIZE)
                value = va_arg(args, size_t);
            else
                value = va_arg(args, unsigned int);

            unsigned int base = specifier == 'u' ? 10U :
                                (specifier == 'o' ? 8U : 16U);
            format_number(output, value, false, base, specifier == 'X', width,
                          precision, left_align, zero_padding);
        } else if (specifier == 'p') {
            uintptr_t value = (uintptr_t)va_arg(args, void*);
            format_number(output, value, false, 16, true, width, precision,
                          left_align, zero_padding);
        } else {
            format_emit(output, '%');
            format_emit(output, specifier);
        }
    }

    if (!output->console && output->buffer != NULL && output->capacity > 0) {
        size_t end = output->count < output->capacity
            ? output->count : output->capacity - 1U;
        output->buffer[end] = '\0';
    }
    return output->count > (size_t)INT_MAX ? INT_MAX : (int)output->count;
}

int printf(const char *format, ...) {
    format_output_t output = { .buffer = NULL, .capacity = 0,
                               .count = 0, .console = true };
    va_list args;
    va_start(args, format);
    int result = format_v(&output, format, args);
    va_end(args);
    return result;
}

int sprintf(char *buffer, const char *format, ...) {
    if (buffer == NULL) return -1;
    format_output_t output = { .buffer = buffer, .capacity = SIZE_MAX,
                               .count = 0, .console = false };
    va_list args;
    va_start(args, format);
    int result = format_v(&output, format, args);
    va_end(args);
    return result;
}

int snprintf(char *buffer, size_t size, const char *format, ...) {
    if (buffer == NULL && size != 0) return -1;
    format_output_t output = { .buffer = buffer, .capacity = size,
                               .count = 0, .console = false };
    va_list args;
    va_start(args, format);
    int result = format_v(&output, format, args);
    va_end(args);
    return result;
}

void hex_dump(const void* data, size_t size) {
    const uint8_t* byte_data = (const uint8_t*)data; // Treat data as byte array
    size_t line_count = 0;                           // Counter to track lines printed
    size_t lines_per_page = 20;
    size_t offset = 0;

    for (size_t i = 0; i < size; i += 16) {
        // Print the offset for the current line
        printf("%08X  ", (unsigned int)(offset + i));

        // Print the hex values (16 per line)
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                printf("%02X ", byte_data[i + j]);
            } else {
                printf("   "); // Print spaces for padding
            }
        }

        // Print ASCII characters
        printf(" ");
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                unsigned char c = byte_data[i + j];
                printf("%c", (isprint(c) ? c : '.')); // Print printable characters or '.'
            }
        }
        printf("\n");

        // Increment line count and check if we've reached the limit for the page
        line_count++;
        if (lines_per_page > 0 && line_count >= lines_per_page) {
            line_count = 0; // Reset line count
            wait_enter_pressed(); // Wait for user input before continuing
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
    delay_ms(duration_ms);
    
    disable_pc_speaker();

    #endif
}
