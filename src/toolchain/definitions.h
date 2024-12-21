#ifndef DEFINITIONS_H
#define DEFINITIONS_H

typedef struct {
    unsigned int startCluster;    // startCluster of the file
    const char* mode;     // Mode the file was opened with
    const char* name;     // Name of the file
    size_t size;          // Size of the file
    size_t position;      // Current position in the file (offset from base)
    char *base;           // Base address of the file in memory
} FILE;

#endif // DEFINITIONS_H