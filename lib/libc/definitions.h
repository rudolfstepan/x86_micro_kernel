#ifndef DEFINITIONS_H
#define DEFINITIONS_H

typedef struct {
    unsigned char *base;  // Base address of the file in memory
    unsigned char *ptr;   // Current read/write position
    unsigned int start_cluster;    // start_cluster of the file
    const char* mode;     // Mode the file was opened with
    const char* name;     // Name of the file
    size_t size;          // Size of the file
    size_t position;      // Current position in the file (offset from base)
} FILE;

#endif // DEFINITIONS_H