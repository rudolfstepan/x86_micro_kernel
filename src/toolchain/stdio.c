#include "stdio.h"
#include "strings.h"
#include "stdlib.h"

#include <stdarg.h>
#include <stddef.h>
#include "drivers/keyboard/keyboard.h"

// -----------------------------------------------------------------
// Directory Handling Functions
// the following functions are defined in the filesystem/fat32/fat32.c file
// -----------------------------------------------------------------
int mkdir(const char* path){
    return create_directory(path);
}

int rmdir(const char* path){
    return delete_directory(path);
}

int readdir(const char *path, char *buffer, unsigned int *size, drive_type_t driveType) {

    if(driveType == DRIVE_TYPE_NONE){
        printf("Invalid drive type\n");
        return -1;
    }

    if(driveType == DRIVE_TYPE_ATA){
        return fat32_read_dir(path, buffer, size);
    }

    if(driveType == DRIVE_TYPE_FDD){
        return fat12_read_dir(path);
    }

    return -1;
}

// -----------------------------------------------------------------
// File Functions
// -----------------------------------------------------------------

File* fopen(const char* filename, const char* mode) {
    return open_file(filename, mode);
}

int fread(void* buffer, int size, File* fd) {
    return read_file(fd, buffer, size);
}

int rmfile(const char* path){
    return delete_file(path);
}

int mkfile(const char* path){
    return create_file(path);
}

// -----------------------------------------------------------------
// Console Functions
// -----------------------------------------------------------------

// Helper function to reverse a string
static void reverse(char *str, int length) {
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
static int int_to_str(int num, char *str, int base) {
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

// Function to print an unsigned integer
void print_unsigned(unsigned int value, unsigned int base) {
    char buffer[32]; // Enough for base-2 representation of 32-bit integer
    int i = 30;

    // Handle 0 explicitly, otherwise it will be printed as an empty string
    if (value == 0) {
        vga_write_char('0');
        return;
    }

    for (; value && i; --i, value /= base) {
        buffer[i] = "0123456789abcdef"[value % base];
    }

    for (i++; i < 31; i++) {
        vga_write_char(buffer[i]);
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
        vga_write_char(*s++);
    }

    s = hexString;
    while (*s) {
        vga_write_char(*s++);
    }
}

void print_hex_padded(unsigned int value, int width) {
    char hex_buffer[33]; // Enough for 32 digits plus a null terminator
    char *ptr = &hex_buffer[32];
    *ptr = '\0';

    do {
        int hex_digit = value & 0xF;
        *--ptr = (hex_digit < 10) ? (hex_digit + '0') : (hex_digit - 10 + 'A');
        value >>= 4;
    } while (value != 0);

    int num_digits = &hex_buffer[32] - ptr;
    for (int i = 0; i < width - num_digits; ++i) {
        vga_write_char('0');
    }

    while (*ptr) {
        vga_write_char(*ptr++);
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
        vga_write_char(buffer[i]);
    }
}

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

            // Parse width value
            while (*format >= '0' && *format <= '9') {
                width = width * 10 + (*format - '0');
                width_specified = true;
                format++;
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

            if (strncmp(format, "llx", 3) == 0) {
                uint64_t llx = va_arg(args, uint64_t);
                print_hex64(llx);  // This assumes print_hex64 handles width.
                format += 2;       // Skip past 'llx'
            } else {
                switch (*format) {
                    case 'c': {
                        char c = (char)va_arg(args, int);
                        vga_write_char(c);
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
                                vga_write_char(s[i]);
                            }
                            for (int i = 0; i < pad; i++) {
                                vga_write_char(' ');
                            }
                        } else {
                            // Print padding first, then the string
                            for (int i = 0; i < pad; i++) {
                                vga_write_char(zero_padding ? '0' : ' ');
                            }
                            for (int i = 0; i < len; i++) {
                                vga_write_char(s[i]);
                            }
                        }
                        break;
                    }
                    case 'u': {
                        unsigned int u = va_arg(args, unsigned int);
                        char buffer[32];
                        int_to_str(u, buffer, 10);
                        int len = strlen(buffer);
                        int pad = width - len;
                        if (width_specified && pad > 0) {
                            for (int i = 0; i < pad; i++) vga_write_char(zero_padding ? '0' : ' ');
                        }
                        char* s = buffer;
                        while (*s) {
                            vga_write_char(*s++);
                        }
                        break;
                    }
                    case 'd': {
                        int i = va_arg(args, int);
                        char buffer[32];
                        int_to_str(i, buffer, 10);
                        int len = strlen(buffer);
                        int pad = width - len;
                        if (width_specified && pad > 0) {
                            for (int i = 0; i < pad; i++) vga_write_char(zero_padding ? '0' : ' ');
                        }
                        char* s = buffer;
                        while (*s) {
                            vga_write_char(*s++);
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
                            vga_write_char(*s++);
                        }
                        break;
                    }
                }
            }
        } else {
            vga_write_char(*format);
        }
        format++;
    }

    va_end(args);
    return 0;
}

// Main sprintf implementation
int sprintf(char *buffer, const char *format, ...) {
    va_list args;
    va_start(args, format);

    char *str = buffer;
    const char *p = format;
    char temp[20];  // Temporary buffer for integer conversion
    int written = 0;

    while (*p != '\0') {
        if (*p == '%') {
            p++;  // Move past '%'

            if (*p == 's') {  // String
                char *arg = va_arg(args, char *);
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
            wait_for_enter(); // Wait for key press
        }
    }
}
