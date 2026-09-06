#ifndef REIST_TERMINAL_INPUT_H
#define REIST_TERMINAL_INPUT_H

#include "kernel/proc/process.h"

#define TERMINAL_INPUT_DEPTH 8U
/* Also protects the keyboard byte queue. Never acquire Process under this. */
extern spinlock_t terminal_input_lock;
bool terminal_input_admitted_locked(int pid, uint32_t generation);
/* Caller holds process-table lock and has validated both live identities. */
int terminal_input_control_locked(Process *caller, Process *target,
                                   const reist_terminal_input_request_t *request);
void terminal_input_process_cleanup(int pid, uint32_t generation);

#endif
