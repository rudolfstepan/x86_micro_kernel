#include "stdio.h"
#include "strings.h"

#include "../kernel/fat32.h"
#include "../kernel/system.h"
#include "../kernel/video.h"

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
// File Functions
// -----------------------------------------------------------------

File* fopen(const char* filename, const char* mode) {
    return open_file(filename, mode);
}

// int fclose(int fd){
//     return close_file(fd);
// }

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


// void vprintf(const char* format, va_list args) {
//     while (*format != '\0') {
//         if (*format == '%') {
//             format++;

//             switch (*format) {
//                 case 'c': {
//                     char c = (char)va_arg(args, int);
//                     vga_write_char(c);
//                     break;
//                 }
//                 case 's': {
//                     char* s = va_arg(args, char*);
//                     while (*s) {
//                         vga_write_char(*s++);
//                     }
//                     break;
//                 }
//                 case 'u': {
//                     unsigned int u = va_arg(args, unsigned int);
//                     print_unsigned(u, 10); // Base 10 for unsigned integer
//                     break;
//                 }
//                 case 'd': {
//                     int i = va_arg(args, int);
//                     char buffer[32];
//                     intToStr(i, buffer, 10);
//                     char* s = buffer;
//                     while (*s) {
//                         vga_write_char(*s++);
//                     }
//                     break;
//                 }
//                 case 'p': {
//                     void* p = va_arg(args, void*);
//                     print_hex((unsigned int)p); // Assuming 32-bit addresses
//                     break;
//                 }
//                 case 'x': {
//                     unsigned int x = va_arg(args, unsigned int);
//                     print_hex_padded(x, 8); // Pad to 8 characters
//                     break;
//                 }
                           
//             }
//         } else {
//             vga_write_char(*format);
//         }
//         format++;
//     }

// }

int printf(const char* format, ...) {
    va_list args;
    va_start(args, format);

    while (*format != '\0') {
        if (*format == '%') {
            format++;

            // Parse width
            int width = 0;
            bool width_specified = false;
            while (*format >= '0' && *format <= '9') {
                width = width * 10 + (*format - '0');
                width_specified = true;
                format++;
            }

            
            if (strncmp(format, "llx", 3) == 0) {
                uint64_t llx = va_arg(args, uint64_t);
                print_hex64(llx);
                format += 2; // Skip past 'llx'
            } else {
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
                    case 'X': {
                        unsigned int x = va_arg(args, unsigned int);
                        if (!width_specified) {
                            width = 8; // Default width for %X
                        }
                        print_hex_padded(x, width);
                        break;
                    }
                }

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