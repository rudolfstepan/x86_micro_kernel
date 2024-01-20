#include "stdio.h"

#include "fat32.h"
#include "system.h"
#include "video.h"

// custom implementation
#include "stdlib.h"

// -----------------------------------------------------------------
// Directory Handling Functions
// -----------------------------------------------------------------
int mkdir(const char* path){
    return create_directory(path);
}

int rmdir(const char* path){
    return delete_directory(path);
}

int chdir(const char* path){
    return change_directory(path);
}

int readdir(const char* path, char* buffer, unsigned int *size){
    return read_directory_to_buffer(path, buffer, size);
}

// -----------------------------------------------------------------
// Console Functions
// -----------------------------------------------------------------


void intToStr(int value, char* str, int base) {
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

void vprintf(const char* format, va_list args) {
    while (*format != '\0') {
        if (*format == '%') {
            format++;
            switch (*format) {
            case 'c': {
                char c = (char)va_arg(args, int);
                vga_write_char(c);
                break;
            }
            case 's': {
                char* s = va_arg(args, char*);
                while (*s) {
                    vga_write_char(*s++);
                }
                break;
            }
            case 'u': {
                unsigned int u = va_arg(args, unsigned int);
                print_unsigned(u, 10); // Base 10 for unsigned integer
                break;
            }
            case 'd': {
                int i = va_arg(args, int);
                char buffer[32];
                intToStr(i, buffer, 10);
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
                    // Add more cases for other specifiers
            }
        } else {
            vga_write_char(*format);
        }
        format++;
    }

}

int printf(const char* format, ...) {
    va_list args;
    va_start(args, format);

    while (*format != '\0') {
        if (*format == '%') {
            format++;
            switch (*format) {
            case 'c': {
                char c = (char)va_arg(args, int);
                vga_write_char(c);
                break;
            }
            case 's': {
                char* s = va_arg(args, char*);
                while (*s) {
                    vga_write_char(*s++);
                }
                break;
            }
            case 'u': {
                unsigned int u = va_arg(args, unsigned int);
                print_unsigned(u, 10); // Base 10 for unsigned integer
                break;
            }
            case 'd': {
                int i = va_arg(args, int);
                char buffer[32];
                intToStr(i, buffer, 10);
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
                    // Add more cases for other specifiers
            }
        } else {
            vga_write_char(*format);
        }
        format++;
    }

    // After printing is done, update cursor position
    va_end(args);

    return 0;
}