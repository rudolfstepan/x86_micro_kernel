#ifndef COMMAND_H
#define COMMAND_H

#include "system.h"
#include "drivers/ata/ata.h"

typedef struct {
    const char *name;
    void (*handler)(int, char**);
} Command;

extern char current_path[256];
extern void process_command(char *input_buffer);

#endif // COMMAND_H