#ifndef COMMAND_H
#define COMMAND_H

#include "system.h"

typedef struct {
    const char *name;
    void (*handler)(int, char**);
} Command;

extern char current_path[256];
extern void process_command(char *input_buffer);
extern void print_prompt();

#endif // COMMAND_H