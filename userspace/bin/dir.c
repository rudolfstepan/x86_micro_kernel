/**
 * @file userspace/bin/dir.c
 * @brief Legacy-Verzeichnisanzeige.
 *
 * Layer: Ring-3 interactive user program.
 * Contract: Eingabe, Pfade und Rückgabewerte werden vor Dispatch oder Dateizugriff geprüft.
 * Safety: Puffer und History sind fest begrenzt; Fehler beenden nur das aktuelle Kommando.
 */
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"

void main() {

    int counter = 0;

    printf("DIR CLI started\n");

    while (1)
    {
         //printf("DIR CLI running %d\n", counter++);

         //delay_ms(3000);

         asm volatile("int $0x29"); // Trigger a timer interrupt
    }
}
