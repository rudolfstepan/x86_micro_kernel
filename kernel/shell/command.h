#ifndef COMMAND_H
#define COMMAND_H

// Main shell loop
void command_loop(void);
void process_command(char *input_buffer);

// Command history functions
void history_add(const char *cmd);
const char* history_get_prev(void);
const char* history_get_next(void);
void history_reset(void);
void history_list(void);

#endif // COMMAND_H