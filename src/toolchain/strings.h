#ifndef STRINGS_H    /* This is an "include guard" */
#define STRINGS_H    /* prevents the file from being included twice. */

#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

// Assuming a maximum path length
#define MAX_PATH_LENGTH 256

// Converts a lowercase character to uppercase.
int toupper(int ch);
// Converts an uppercase character to lowercase.
int tolower(int ch);
// Checks if the character is alphanumeric (either a digit or a letter).
int isalnum(int c);
// Checks if the character is a digit (0-9).
int isdigit(int c);
// Checks if the character is a whitespace character (e.g., space, tab, newline).
int isspace(int c);
// Checks if the character is an alphabetic letter (either uppercase or lowercase).
int isalpha(int c);
// Returns the length of a string (number of characters before the null terminator).
size_t strlen(const char* str);
// Appends the source string to the destination string.
char* strcat(char* dest, const char* src);
// Appends the first 'n' characters of the source string to the destination string.
char* strncat(char* dest, const char* src, size_t n);
// Copies the source string to the destination string.
char* strcpy(char* dest, const char* src);
// Compares two strings lexicographically.
int strcmp(const char* str1, const char* str2);
// Copies up to 'num' characters from the source string to the destination string.
char* strncpy(char* dest, const char* src, size_t num);
// Compares up to 'n' characters of two strings lexicographically.
int strncmp(const char* str1, const char* str2, size_t n);
// Compares up to 'n' characters of two strings lexicographically, ignoring case.
int strncasecmp(const char* str1, const char* str2, size_t n);
// Tokenizes a string into smaller substrings using delimiters, thread-safe version.
char* strtok_r(char* str, const char* delim, char** saveptr);
// Returns the length of the initial segment of str1 that contains only characters from str2.
size_t strspn(const char* str1, const char* str2);
// Returns the length of the initial segment of str1 that contains none of the characters from str2.
size_t strcspn(const char* str1, const char* str2);
// Writes formatted data to a string, limiting the output to 'size' characters.
int snprintf(char* str, size_t size, const char* format, ...);
// Converts the initial part of a string to an unsigned long integer based on the given base.
unsigned long strtoul(const char* str, char** endptr, int base);
// Searches for the first occurrence of the character 'c' in the string 'str'.
char* strchr(const char* str, int c);
// Tokenizes a string into smaller substrings using delimiters (not thread-safe).
char* strtok(char* str, const char* delimiters);

// Custom non-standard functions:
// Splits an input string into a command and arguments.
int split_input(const char* input, char* command, char** arguments, int max_length, int max_args);
// Converts a string to lowercase in place.
void str_to_lower(char* str);
// Converts a string to uppercase in place.
void str_to_upper(char* str);
// Normalizes a given file path by adjusting it based on the current path.
void normalize_path(char* input_path, char* normalized_path, const char* current_path);
// Trims trailing spaces from a string in place.
void trim_trailing_spaces(char* str);
// Converts an integer value to a hexadecimal string representation.
void int_to_hex_str(unsigned int value, char* buffer, int width, bool zero_padding);
// Normalizes a string by copying 'length' characters from 'src' to 'dest' with potential adjustments.
void normalize_string(char* dest, const char* src, size_t length);
// Trims a specific character from the end of a string in place.
void str_trim_end(char* str, char ch);
// Trims spaces from the input string and copies the result to the output buffer.
void str_trim_spaces(const char* input, char* output, int max_len);


// char toupper(char ch);
// char tolower(char ch);
// int isalnum(int c);
// int isdigit(int c);
// int isspace(int c);
// int isalpha(int c);
// int strlen(const char* str);
// void strcat(char* dest, const char* src);
// char* strncat(char* dest, const char* src, size_t n);
// void strcpy(char* dest, const char* src);
// int strcmp(const char* str1, const char* str2);
// void strncpy(char* dest, const char* src, int num);
// int strncmp(const char *str1, const char *str2, size_t n);
// int strncasecmp(const char *str1, const char *str2, size_t n);
// int split_input(const char* input, char* command, char** arguments, int max_length, int max_args);
// void str_to_lower(char* str);
// void str_to_upper(char* str);
// char* strtok_r(char* str, const char* delim, char** saveptr);
// size_t strspn(const char* str1, const char* str2);
// size_t strcspn(const char* str1, const char* str2);
// int snprintf(char *str, size_t size, const char *format, ...);
// unsigned long strtoul(const char *str, char **endptr, int base);
// void normalize_path(char* input_path, char* normalized_path, const char* current_path);
// void trim_trailing_spaces(char *str);
// void int_to_hex_str(unsigned int value, char* buffer, int width, bool zero_padding);
// void normalize_string(char* dest, const char* src, size_t length);
// char* strchr(const char* str, char c);
// void str_trim_end(char *str, char ch);
// void str_trim_spaces(const char* input, char* output, int max_len);
// char* strtok(char* str, const char* delimiters);

#endif /* STRINGS_H */