#ifndef COMMAND_H
#define COMMAND_H

#include "system.h"
#include "drivers/ata/ata.h"

typedef struct {
    const char *name;
    void (*handler)(int, char**);
} Command;

extern void process_command(char *input_buffer);
extern void print_prompt();

#endif // COMMAND_H