#ifndef STRINGS_H    /* This is an "include guard" */
#define STRINGS_H    /* prevents the file from being included twice. */

#include <stddef.h>
#include <stdarg.h>


char toupper(char ch);
int isalnum(int c);
int isdigit(int c);
int isspace(int c);
int isalpha(int c);

int strlen(const char* str);
void strcat(char* dest, const char* src);
char* strncat(char* dest, const char* src, size_t n);
void strcpy(char* dest, const char* src);
int strcmp(const char* str1, const char* str2);
void strncpy(char* dest, char* src, int num);
int strncmp(const char *str1, const char *str2, size_t n);
int split_input(const char *input, char *command, char arguments[][50], int max_length, int max_args);

char* strtok_r(char* str, const char* delim, char** saveptr);
size_t strspn(const char* str1, const char* str2);
size_t strcspn(const char* str1, const char* str2);
int snprintf(char *str, size_t size, const char *format, ...);

unsigned long strtoul(const char *str, char **endptr, int base);


#endif /* STRINGS_H */