/**
 * @file userspace/sdk/crt0.c
 * @brief Ring-3-Startcode und definierter Prozessausstieg.
 *
 * Layer: Ring-3 SDK and protocol support.
 * Contract: Größen, Versionen und Pufferbereiche werden vor Lesen, Schreiben oder Syscall geprüft.
 * Safety: Parser und Formatierung sind kapazitätsbegrenzt; Fehler erzeugen keine partiellen Ausgaben.
 */
#include "x86os.h"

extern int main(int argc, char** argv);

__attribute__((section(".text._start"), noreturn))
void _start(int argc, char** argv) {
    int status = main(argc, argv);
    x86os_exit(status);
}
