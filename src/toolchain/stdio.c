#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include <stdarg.h>
#include <stddef.h>
#include "filesystem/filesystem.h"
#include "filesystem/fat32/fat32.h"
#include "filesystem/fat12/fat12.h"
#include "drivers/io/io.h"
// #include "drivers/video/vga.h"
#include "drivers/video/framebuffer.h"
#include "drivers/kb/kb.h"


#define BUFFER_SIZE 128

// for memory dump
#define BYTES_PER_LINE 16
#define MAX_LINES 20

// PIT I/O port addresses
#define PIT_CONTROL_PORT 0x43
#define PIT_CHANNEL_2_PORT 0x42
#define PC_SPEAKER_PORT 0x61


#ifdef __KERNEL__ // Kernel mode (Ring 0) implementation, gcc will define this macro
int is_kernel_context() {
    return 1; // Immer Kernel-Kontext im Kernel selbst
}
#else
int is_kernel_context() {
    return 0; // Immer User-Kontext im User-Modus
    // unsigned short cs;
    // asm volatile ("mov %%cs, %0" : "=r" (cs));
    // return (cs & 3) == 0; // CPL prüfen
}
#endif

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

int isprint(int c) {
    // Check if c is a printable ASCII character (32 to 126)
    return (c >= 32 && c <= 126);
}

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
        //vga_write_char(c);
        fb_write_char(c);
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

void print_char(char c) {
    putchar(c);
}

void print_string(const char *str) {
    while (*str) {
        putchar(*str++);
    }
}

void print_number(uint64_t num, int base, bool is_signed, bool uppercase, bool alt_form, int width, int precision, bool zero_pad, bool left_align, bool always_sign) {
    char buffer[BUFFER_SIZE];
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int index = BUFFER_SIZE - 1;
    buffer[index] = '\0';

    // Generate digits in reverse order
    do {
        buffer[--index] = digits[num % base];
        num /= base;
    } while (num > 0);

    // Add prefix for alternate form
    if (alt_form && base == 16) {
        buffer[--index] = uppercase ? 'X' : 'x';
        buffer[--index] = '0';
    }

    // Print padding and number
    int len = BUFFER_SIZE - 1 - index;
    if (!left_align) while (width-- > len) putchar(zero_pad ? '0' : ' ');
    print_string(&buffer[index]);
    if (left_align) while (width-- > len) putchar(' ');
}

// Helper to convert integer to string and print
void print_formatted_number(uint64_t num, int base, bool is_signed, bool uppercase, bool alt_form, int width, int precision, bool zero_pad, bool left_align, bool always_sign) {
    print_number(num, base, is_signed, uppercase, alt_form, width, precision, zero_pad, left_align, always_sign);
}

void print_float(double value, int precision, int width, bool left_align, bool always_sign);

int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);

    while (*format) {
        if (*format == '%') {
            format++;  // Skip '%'

            // Flags
            bool left_align = false, zero_pad = false, always_sign = false, space_pad = false, alt_form = false;

            // Parse flags
            while (*format == '-' || *format == '0' || *format == '+' || *format == ' ' || *format == '#') {
                switch (*format) {
                    case '-': left_align = true; break;
                    case '0': zero_pad = true; break;
                    case '+': always_sign = true; break;
                    case ' ': space_pad = true; break;
                    case '#': alt_form = true; break;
                }
                format++;
            }

            // Parse width
            int width = 0;
            if (*format == '*') {
                width = va_arg(args, int);
                format++;
            } else {
                while (*format >= '0' && *format <= '9') {
                    width = width * 10 + (*format - '0');
                    format++;
                }
            }

            // Parse precision
            int precision = -1; // Default is -1 (no precision)
            if (*format == '.') {
                precision = 0;
                format++;
                if (*format == '*') {
                    precision = va_arg(args, int);
                    format++;
                } else {
                    while (*format >= '0' && *format <= '9') {
                        precision = precision * 10 + (*format - '0');
                        format++;
                    }
                }
            }

            // Length modifier
            bool is_long = false, is_long_long = false;
            if (*format == 'l') {
                format++;
                if (*format == 'l') {
                    is_long_long = true;
                    format++;
                } else {
                    is_long = true;
                }
            }

            // Format specifier
            switch (*format) {
                case 'd': case 'i': { // Signed integers
                    int64_t value = is_long_long ? va_arg(args, int64_t) :
                                    is_long ? va_arg(args, long) :
                                    va_arg(args, int);
                    print_formatted_number(value, 10, true, false, false, width, precision, zero_pad, left_align, always_sign);
                    break;
                }
                case 'u': { // Unsigned integers
                    uint64_t value = is_long_long ? va_arg(args, uint64_t) :
                                     is_long ? va_arg(args, unsigned long) :
                                     va_arg(args, unsigned int);
                    print_formatted_number(value, 10, false, false, false, width, precision, zero_pad, left_align, false);
                    break;
                }
                case 'x': case 'X': { // Hexadecimal
                    uint64_t value = is_long_long ? va_arg(args, uint64_t) :
                                     is_long ? va_arg(args, unsigned long) :
                                     va_arg(args, unsigned int);
                    print_formatted_number(value, 16, false, (*format == 'X'), alt_form, width, precision, zero_pad, left_align, false);
                    break;
                }
                case 'f': { // Floating-point numbers
                    double value = va_arg(args, double); // Retrieve as double
                    if (precision == -1) precision = 6; // Default precision is 6
                    print_float(value, precision, width, left_align, always_sign);
                    break;
                }
                case 'z': { // 'z' modifier for size_t
                    format++; // Skip 'z'
                    if (*format == 'u') { // Handle %zu
                        size_t value = va_arg(args, size_t);
                        print_formatted_number(value, 10, false, false, false, width, precision, zero_pad, left_align, false);
                    }
                    break;
                }
                case 'p': { // Pointers
                    uintptr_t ptr = va_arg(args, uintptr_t);
                    print_string("0x");
                    print_formatted_number(ptr, 16, false, false, false, width, precision, true, left_align, false);
                    break;
                }
                case 'c': { // Characters
                    char c = (char)va_arg(args, int);
                    print_char(c);
                    break;
                }
                case 's': { // Strings
                    const char *str = va_arg(args, const char *);
                    print_string(str);
                    break;
                }
                case '%': { // Literal '%'
                    print_char('%');
                    break;
                }
                default:
                    print_char(*format); // Print unknown specifier
                    break;
            }
        } else {
            print_char(*format); // Print non-format character
        }
        format++;
    }

    va_end(args);
    return 0;
}

// Helper function for printing floats
void print_float(double value, int precision, int width, bool left_align, bool always_sign) {
    char buffer[64];
    int len = 0;

    // Format the float to a string
    if (always_sign && value >= 0.0) {
        snprintf(buffer, sizeof(buffer), "+%.*f", precision, value);
    } else {
        snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
    }

    len = strlen(buffer);

    // Handle right alignment
    if (!left_align && len < width) {
        for (int i = 0; i < width - len; i++) {
            print_char(' ');
        }
    }

    // Print the float string
    print_string(buffer);

    // Handle left alignment
    if (left_align && len < width) {
        for (int i = 0; i < width - len; i++) {
            print_char(' ');
        }
    }
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

void format_float(double value, int precision, char* buffer, size_t size) {
    if (size == 0) return;

    int integer_part = (int)value;  // Extract integer part
    double fractional_part = value - (double)integer_part;  // Extract fractional part

    // Handle negative numbers
    int pos = 0;
    if (value < 0) {
        buffer[pos++] = '-';
        integer_part = -integer_part;
        fractional_part = -fractional_part;
    }

    // Convert integer part
    char temp[32];
    int temp_pos = 0;
    do {
        temp[temp_pos++] = '0' + (integer_part % 10);
        integer_part /= 10;
    } while (integer_part > 0);

    // Reverse the integer part into the buffer
    while (temp_pos > 0 && pos < size - 1) {
        buffer[pos++] = temp[--temp_pos];
    }

    // Add the decimal point
    if (pos < size - 1) buffer[pos++] = '.';

    // Convert fractional part
    for (int i = 0; i < precision; i++) {
        fractional_part *= 10;
        int digit = (int)fractional_part;
        if (pos < size - 1) {
            buffer[pos++] = '0' + digit;
        }
        fractional_part -= digit;
    }

    // Null-terminate the buffer
    buffer[pos] = '\0';
}

// Simple implementation of snprintf
int snprintf(char* str, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);

    unsigned int written = 0;
    const char* p = format;

    while (*p != '\0' && written < size - 1) { // Reserve space for null terminator
        if (*p == '%') {
            p++;  // Skip '%'

            int precision = 6;  // Default precision for floats
            if (*p == '.') {    // Precision parsing
                p++;
                if (*p == '*') {
                    precision = va_arg(args, int);
                    p++;
                } else {
                    precision = 0;
                    while (*p >= '0' && *p <= '9') {
                        precision = precision * 10 + (*p - '0');
                        p++;
                    }
                }
            }

            if (*p == 's') {  // String format
                const char* arg = va_arg(args, const char*);
                while (*arg != '\0' && written < size - 1) {
                    str[written++] = *arg++;
                }
            } else if (*p == 'd') {  // Integer format
                int num = va_arg(args, int);
                char num_buffer[12];  // Buffer to hold the integer as string
                int num_written = 0;

                if (num < 0) {
                    if (written < size - 1) str[written++] = '-';
                    num = -num;
                }

                do {
                    num_buffer[num_written++] = (num % 10) + '0';
                    num /= 10;
                } while (num > 0 && num_written < (int)sizeof(num_buffer));

                for (int i = num_written - 1; i >= 0 && written < size - 1; i--) {
                    str[written++] = num_buffer[i];
                }
            } else if (*p == 'f') {  // Floating-point format
                double value = va_arg(args, double);
                char float_buffer[64];
                format_float(value, precision, float_buffer, sizeof(float_buffer));

                for (int i = 0; float_buffer[i] != '\0' && written < size - 1; i++) {
                    str[written++] = float_buffer[i];
                }
            }
            else if (*p == '%') {  // Literal '%'
                if (written < size - 1) str[written++] = '%';
            } else {  // Unsupported specifier
                if (written < size - 1) str[written++] = '%';
                if (written < size - 1) str[written++] = *p;
            }
        } else {
            if (written < size - 1) str[written++] = *p;
        }
        p++;
    }

    va_end(args);

    // Null-terminate the string
    if (size > 0) {
        str[written] = '\0';
    }

    return written;
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
