#include "strings.h"
#include <limits.h>


// Helper function to convert a character to uppercase
char toupper(char ch) {
    return (ch >= 'a' && ch <= 'z') ? (ch - 'a' + 'A') : ch;
}

int isalnum(int c) {
    // Check if the character is a letter (uppercase or lowercase) or a digit
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

int isdigit(int c) {
    return (c >= '0' && c <= '9');
}

int isspace(int c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

int isalpha(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

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
    while (input[i] != ' ' && i <= max_length) {
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

unsigned long strtoul(const char *str, char **endptr, int base) {
    if (base != 0 && (base < 2 || base > 36)) {
        if (endptr) *endptr = (char *)str;
        return 0; // Base out of range
    }

    int result = 0;
    int digit;
    int cutoff = ULONG_MAX / base;
    int cutlim = ULONG_MAX % base;

    // Skip white spaces
    while (isspace((unsigned char)*str)) str++;

    // Determine the base if not specified
    if (base == 0) {
        if (*str == '0') {
            str++;
            if (*str == 'x' || *str == 'X') {
                base = 16;
                str++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    }

    while (*str) {
        if (isdigit((unsigned char)*str))
            digit = *str - '0';
        else if (isalpha((unsigned char)*str))
            digit = toupper((unsigned char)*str) - 'A' + 10;
        else
            break;

        if (digit >= base) break;

        // Check for overflow
        if (result > cutoff || (result == cutoff && digit > cutlim)) {
            result = ULONG_MAX; // Set to maximum value on overflow
            break;
        }

        result = result * base + digit;
        str++;
    }

    if (endptr) *endptr = (char *)str;

    return result;
}

int strncmp(const char *str1, const char *str2, size_t n) {
    while (n-- && *str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }

    if (n == (size_t)-1) {
        return 0;
    } else {
        return (unsigned char)*str1 - (unsigned char)*str2;
    }
}

