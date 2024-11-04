#ifndef STDIO_H
#define STDIO_H

#include "filesystem/filesystem.h"
#include "filesystem/fat32/fat32.h"
#include "filesystem/fat12/fat12.h"
#include "drivers/video/video.h"


// File Handling Functions
File* fopen(const char* filename, const char* mode);
int fread(void* buffer, int size, File* fd);
int rmfile(const char* path);                                   // remove file
int mkfile(const char* path);                                   // create file

// Directory Handling Functions
int mkdir(const char* path);                                    // make directory
int rmdir(const char* path);                                    // remove directory
//bool chdir(const char* path);                                    // change directory
//bool change_drive(const char* input);
void get_full_path(const char* path, char* full_path, size_t size);
int readdir(const char* path, char* buffer, unsigned int *size, drive_type_t driveType);// read directory

// Console Functions
int printf(const char* format, ...);                            // print formatted string
int sprintf(char *buffer, const char *format, ...);

void hex_dump(const unsigned char* data, size_t size);

#endif // STDIO_H