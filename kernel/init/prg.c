/**
 * @file kernel/init/prg.c
 * @brief Lädt und startet validierte PRG-Programme aus dem Systemdateisystem.
 *
 * Layer: Ring-0 program loader.
 * Contract: Header, Größen, Zielbereiche und Einstiegspunkt werden vor Publikation geprüft.
 * Safety: Fehlerhafte Images erzeugen keinen teilweise gestarteten Prozess.
 */
#include "kernel/init/prg.h"

/* MYPR v1 parsing lives in kernel/proc/program_image.c.  This translation
 * unit intentionally contains only the shared format assertions from prg.h;
 * historical in-kernel ELF and relocation loaders were never valid runtime
 * paths and have been removed. */
