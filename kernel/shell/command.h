/**
 * @file kernel/shell/command.h
 * @brief Rettungsshell-Parservertrag.
 *
 * Layer: Ring-0 rescue support.
 * Contract: Überlange oder unbekannte Kommandos bleiben ohne Seiteneffekt.
 * Safety: Fehler werden vor sichtbaren Seiteneffekten abgewiesen; Arbeit und Speicher sind begrenzt.
 */
#ifndef KERNEL_SHELL_COMMAND_H
#define KERNEL_SHELL_COMMAND_H

#include <stdbool.h>

// Function prototypes
void process_command(char *input_buffer);
void show_prompt(void);
void command_loop(void);

#endif // KERNEL_SHELL_COMMAND_H
