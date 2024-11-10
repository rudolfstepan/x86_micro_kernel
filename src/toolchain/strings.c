#include "strings.h"
#include "stdlib.h"
#include <stdbool.h>

#define ULONG_MAX 4294967295UL

// Converts an uppercase character to lowercase
int tolower(int ch) {
    return (ch >= 'A' && ch <= 'Z') ? (ch - 'A' + 'a') : ch;
}

// Converts a lowercase character to uppercase
int toupper(int ch) {
    return (ch >= 'a' && ch <= 'z') ? (ch - 'a' + 'A') : ch;
}

// Checks if the character is alphanumeric (a letter or a digit)
int isalnum(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

// Checks if the character is a digit (0-9)
int isdigit(int c) {
    return (c >= '0' && c <= '9');
}

// Checks if the character is a whitespace character
int isspace(int c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

// Checks if the character is alphabetic (a letter)
int isalpha(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// Calculates the length of a string
size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

// Copies a string from src to dest
char* strcpy(char* dest, const char* src) {
    char* original_dest = dest;

    while (*src != '\0') {
        *dest++ = *src++;
    }
    *dest = '\0'; // Null-terminate the destination string

    return original_dest;
}

// Copies up to num characters from src to dest, null-padding if needed
char* strncpy(char* dest, const char* src, size_t num) {
    size_t i;
    for (i = 0; i < num && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < num; i++) {
        dest[i] = '\0'; // Null-padding
    }
    return dest;
}

// Concatenates src to the end of dest
char* strcat(char* dest, const char* src) {
    char* original_dest = dest;

    while (*dest != '\0') {
        dest++;
    }
    while (*src != '\0') {
        *dest++ = *src++;
    }
    *dest = '\0'; // Null-terminate

    return original_dest;
}

// Concatenates up to n characters from src to the end of dest
char* strncat(char* dest, const char* src, size_t n) {
    char* ptr = dest + strlen(dest);

    while (*src != '\0' && n--) {
        *ptr++ = *src++;
    }
    *ptr = '\0'; // Null-terminate

    return dest;
}

// Compares two strings lexicographically
int strcmp(const char* str1, const char* str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(unsigned char*)str1 - *(unsigned char*)str2;
}

// Compares up to n characters of two strings lexicographically
int strncmp(const char* str1, const char* str2, size_t n) {
    while (n-- && *str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return (n == (size_t)-1) ? 0 : (*(unsigned char*)str1 - *(unsigned char*)str2);
}

// Case-insensitive comparison of up to n characters of two strings
int strncasecmp(const char* str1, const char* str2, size_t n) {
    while (n-- && *str1 && (tolower((unsigned char)*str1) == tolower((unsigned char)*str2))) {
        str1++;
        str2++;
    }
    return (n == (size_t)-1) ? 0 : (tolower((unsigned char)*str1) - tolower((unsigned char)*str2));
}

// Searches for the first occurrence of a character in a string
char* strchr(const char* str, int c) {
    while (*str != '\0') {
        if (*str == c) {
            return (char*)str;  // Return pointer to the first occurrence
        }
        str++;
    }
    return (c == '\0') ? (char*)str : NULL; // Handle null character
}

// Converts a string to lowercase
void str_to_lower(char* str) {
    if (str == NULL) return;
    for (size_t i = 0; str[i] != '\0'; i++) {
        str[i] = tolower(str[i]);
    }
}

// Converts a string to uppercase
void str_to_upper(char* str) {
    if (str == NULL) return;
    for (size_t i = 0; str[i] != '\0'; i++) {
        str[i] = toupper(str[i]);
    }
}

// Splits an input string into a command and arguments
int split_input(const char* input, char* command, char** arguments, int max_args, int max_length) {
    int i = 0, j = 0, arg_count = 0;

    while (input[i] == ' ' && input[i] != '\0') {
        i++;
    }

    while (input[i] != ' ' && input[i] != '\0' && i < max_length) {
        command[i] = input[i];
        i++;
    }
    command[i] = '\0';
    if (input[i] != '\0') i++;

    while (input[i] != '\0' && arg_count < max_args) {
        if (input[i] == ' ') {
            if (j != 0) {
                arguments[arg_count][j] = '\0';
                arg_count++;
                j = 0;
            }
            i++;
            continue;
        }

        if (j == 0) {
            arguments[arg_count] = malloc(max_length);
            if (arguments[arg_count] == NULL) {
                return arg_count;
            }
        }

        if (j < max_length - 1) {
            arguments[arg_count][j] = input[i];
            j++;
        }
        i++;
    }

    if (j != 0) {
        arguments[arg_count][j] = '\0';
        arg_count++;
    }

    return arg_count;
}

// Tokenizes a string using a delimiter (reentrant version)
char* strtok_r(char* str, const char* delim, char** saveptr) {
    if (str == NULL) {
        str = *saveptr;
    }

    str += strspn(str, delim);
    if (*str == '\0') {
        *saveptr = str;
        return NULL;
    }

    char* end = str + strcspn(str, delim);
    if (*end == '\0') {
        *saveptr = end;
        return str;
    }

    *end = '\0';
    *saveptr = end + 1;
    return str;
}

// Returns the length of the initial segment of str1 containing only characters in str2
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

// Scans str1 for the first occurrence of any characters in str2
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

// Converts a string to an unsigned long integer
unsigned long strtoul(const char* str, char** endptr, int base) {
    if (base != 0 && (base < 2 || base > 36)) {
        if (endptr) *endptr = (char*)str;
        return 0;
    }

    int result = 0;
    int digit;
    int cutoff = ULONG_MAX / base;
    int cutlim = ULONG_MAX % base;

    while (isspace((unsigned char)*str)) str++;

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

        if (result > cutoff || (result == cutoff && digit > cutlim)) {
            result = ULONG_MAX;
            break;
        }

        result = result * base + digit;
        str++;
    }

    if (endptr) *endptr = (char*)str;

    return result;
}

// Normalizes a file path
void normalize_path(char* input_path, char* normalized_path, const char* current_path) {
    if (input_path[0] == '/') {
        strncpy(normalized_path, input_path, MAX_PATH_LENGTH - 1);
    } else {
        if (strcmp(current_path, "/") == 0) {
            snprintf(normalized_path, MAX_PATH_LENGTH, "/%s", input_path);
        } else {
            snprintf(normalized_path, MAX_PATH_LENGTH, "%s/%s", current_path, input_path);
        }
    }
    normalized_path[MAX_PATH_LENGTH - 1] = '\0';
}

// Trims trailing spaces from a string
void trim_trailing_spaces(char* str) {
    int len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

// Helper function to convert an integer to a hexadecimal string
int int_to_hex_str2(unsigned int value, char* buffer, bool uppercase) {
    const char* hex_digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char temp[32];
    int length = 0;

    // Convert to hex, starting from the least significant digit
    do {
        temp[length++] = hex_digits[value % 16];
        value /= 16;
    } while (value != 0);

    // Reverse the string into the buffer
    for (int i = 0; i < length; i++) {
        buffer[i] = temp[length - 1 - i];
    }
    buffer[length] = '\0';

    return length;
}

// Converts an integer to a hexadecimal string
void int_to_hex_str(unsigned int value, char* buffer, int width, bool zero_padding) {
    const char hex_digits[] = "0123456789ABCDEF";
    char temp[32];
    int index = 0;

    do {
        temp[index++] = hex_digits[value % 16];
        value /= 16;
    } while (value > 0);

    int padding = (width > index) ? width - index : 0;
    int buffer_index = 0;

    for (int i = 0; i < padding; i++) {
        buffer[buffer_index++] = zero_padding ? '0' : ' ';
    }

    while (index > 0) {
        buffer[buffer_index++] = temp[--index];
    }

    buffer[buffer_index] = '\0';
}

// Normalizes a string and trims trailing spaces
void normalize_string(char* dest, const char* src, size_t length) {
    for (size_t i = 0; i < length; i++) {
        dest[i] = src[i];
    }

    for (size_t i = length; i > 0; i--) {
        if (dest[i - 1] != ' ') {
            break;
        }
        dest[i - 1] = '\0';
    }

    dest[length] = '\0';
}

// Trims specified trailing characters from a string
void str_trim_end(char* str, char ch) {
    size_t len = strlen(str);
    while (len > 0 && str[len - 1] == ch) {
        str[--len] = '\0';
    }
}

// Trims spaces from the input and writes the result to output
void str_trim_spaces(const char* input, char* output, int max_len) {
    int j = 0;
    for (int i = 0; i < max_len; i++) {
        if (input[i] != ' ' && input[i] != '\0') {
            output[j++] = input[i];
        }
    }
    output[j] = '\0';
}

// Tokenizes a string using a delimiter (non-reentrant)
char* strtok(char* str, const char* delimiters) {
    static char* last = NULL;
    if (str) {
        last = str;
    } else if (!last) {
        return NULL;
    }

    while (*last && strchr(delimiters, *last)) {
        last++;
    }

    if (*last == '\0') {
        last = NULL;
        return NULL;
    }

    char* token_start = last;

    while (*last && !strchr(delimiters, *last)) {
        last++;
    }

    if (*last) {
        *last = '\0';
        last++;
    } else {
        last = NULL;
    }

    return token_start;
}

// POSIX-compliant implementation of strstr
char* strstr(const char* haystack, const char* needle) {
    // Return haystack if needle is an empty string
    if (*needle == '\0') {
        return (char*)haystack;
    }

    // Iterate over the haystack string
    for (const char* h = haystack; *h != '\0'; h++) {
        const char* h_iter = h;
        const char* n_iter = needle;

        // Check if the substring matches at this position
        while (*h_iter != '\0' && *n_iter != '\0' && *h_iter == *n_iter) {
            h_iter++;
            n_iter++;
        }

        // If we reached the end of the needle, we found a match
        if (*n_iter == '\0') {
            return (char*)h;
        }
    }

    // No match found, return NULL
    return NULL;
}
