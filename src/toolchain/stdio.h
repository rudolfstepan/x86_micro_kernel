#ifndef STDIO_H
#define STDIO_H

#include <stddef.h>
#include <stdint.h>
#include "definitions.h"


// File Handling Functions
int mkfile(const char* path);                                   // create file

// Directory Handling Functions
//void get_full_path(const char* path, char* full_path, size_t size);
//int readdir(const char* path, char* buffer, unsigned int *size, drive_type_t driveType);// read directory
int readdir(const char* path, char* buffer, unsigned int *size, uint8_t driveType);// read directory

// Console Functions
int sprintf(char *buffer, const char *format, ...);

// POSIX File Handling Functions
FILE* fopen(const char* filename, const char* mode);           // Open a file
size_t fread(void* buffer, size_t size, size_t count, FILE* stream); // Read from a file
int remove(const char* path);                                  // Remove a file

// POSIX Directory Handling Functions
int mkdir(const char* path, uint8_t mode);                      // Make a directory
int rmdir(const char* path);                                   // Remove a directory
int chdir(const char* path);                                   // Change the current working directory
//char* realpath(const char* path, char* resolved_path);         // Get the full path

// POSIX Directory Reading Functions
//struct dirent* readdir(DIR* dirp);                             // Read a directory entry

// POSIX Console Functions
int printf(const char* format, ...);                           // Print formatted string to standard output
int snprintf(char* buffer, size_t size, const char* format, ...); // Print formatted string to a buffer safely

void hex_dump(const unsigned char* data, size_t size);

void beep(uint32_t frequency, uint32_t duration_ms);

#endif // STDIO_H