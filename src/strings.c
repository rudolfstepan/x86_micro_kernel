#include "strings.h"



void strncpy(char* dest, char* src, int num) {
    for (int i = 0; i < num; i++) {
        dest[i] = src[i];
    }
    dest[num] = '\0'; // Null-terminate the string
}

int strcmp(const char* str1, const char* str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(unsigned char*)str1 - *(unsigned char*)str2;
}

int strlen(const char* str) {
    int len = 0;
    while (str[len]) {
        len++;
    }
    return len;
}

void strcpy(char* dest, const char* src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

void strcat(char* dest, const char* src) {
    while (*dest) {
        dest++;
    }
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

/**
 * Custom implementation of strncat for bare-metal environments.
 *
 * @param dest The destination string to which 'src' is appended.
 * @param src The source string to append to 'dest'.
 * @param n The maximum number of characters to append.
 * @return A pointer to the resulting string 'dest'.
 */
char* strncat(char* dest, const char* src, size_t n) {
    char* ptr = dest + strlen(dest); // Find the end of 'dest'

    // Append characters from 'src' to 'dest'
    while (*src != '\0' && n--) {
        *ptr++ = *src++;
    }

    // Null-terminate 'dest'
    *ptr = '\0';

    return dest;
}

// Custom function to split input into command and arguments
int split_input(const char* input, char* command, char arguments[][50], int max_length, int max_args) {
    int i = 0, j = 0, arg_count = 0;

    // Skip initial whitespace
    while (input[i] == ' ' && input[i] != '\0') {
        i++;
    }

    // Extract command
    while (input[i] != ' ' && i < max_length) {
        command[i] = input[i];
        i++;
    }
    command[i] = '\0'; // Null-terminate the command
    i++; // Move past the space after the command

    // Process arguments
    while (input[i] != '\0' && arg_count < max_args) {
        if (input[i] == ' ') {
            if (j != 0) { // End of an argument
                arguments[arg_count][j] = '\0'; // Null-terminate the argument
                arg_count++;
                j = 0;
            }
            i++; // Skip the space
            continue;
        }

        if (j < 49) { // Check to prevent buffer overflow
            arguments[arg_count][j] = input[i];
            j++;
        }
        i++;
    }

    if (j != 0) { // Handle the last argument
        arguments[arg_count][j] = '\0';
        arg_count++;
    }

    return arg_count; // Return the number of arguments
}

char* strtok_r(char* str, const char* delim, char** saveptr) {
    if (str == NULL) {
        str = *saveptr;
    }

    // Skip leading delimiters
    str += strspn(str, delim);
    if (*str == '\0') {
        *saveptr = str;
        return NULL;
    }

    // Find the end of the token
    char* end = str + strcspn(str, delim);
    if (*end == '\0') {
        *saveptr = end;
        return str;
    }

    // Terminate the token and update saveptr
    *end = '\0';
    *saveptr = end + 1;
    return str;
}

// Helper function: calculates the length of the initial segment of str1 which consists only of characters that are part of str2.
size_t strspn(const char* str1, const char* str2) {
    const char* p;
    const char* a;
    for (p = str1; *p != '\0'; ++p) {
        for (a = str2; *a != '\0'; ++a) {
            if (*p == *a) {
                break;
            }
        }
        if (*a == '\0') {
            return p - str1;
        }
    }
    return p - str1;
}

// Helper function: scans str1 for the first occurrence of any of the characters that are part of str2, returning the number of characters of str1 read before this first occurrence.
size_t strcspn(const char* str1, const char* str2) {
    const char* p;
    const char* a;
    for (p = str1; *p != '\0'; ++p) {
        for (a = str2; *a != '\0'; ++a) {
            if (*p == *a) {
                return p - str1;
            }
        }
    }
    return p - str1;
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
            }
            // Add other format specifiers as needed (e.g., %d for integers)
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
