#ifndef STDIO_H
#define STDIO_H

#include "filesystem/filesystem.h"
#include "filesystem/fat32/fat32.h"
#include "drivers/video/video.h"


// Default drive and path
// a full path is a combination of the drive and the path
extern char current_drive[16];   // Default drive
extern char current_path[256];      // Default path

// File Handling Functions
File* fopen(const char* filename, const char* mode);
int fread(void* buffer, int size, File* fd);
int rmfile(const char* path);                                   // remove file
int mkfile(const char* path);                                   // create file

// Directory Handling Functions
int mkdir(const char* path);                                    // make directory
int rmdir(const char* path);                                    // remove directory
bool chdir(const char* path);                                    // change directory
bool change_drive(const char* input);
void get_full_path(const char* path, char* full_path, size_t size);
int readdir(const char* path, char* buffer, unsigned int *size);// read directory

// Console Functions
int printf(const char* format, ...);                            // print formatted string

#endif // STDIO_H