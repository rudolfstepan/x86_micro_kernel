/**
 * @file kernel/proc/program_image.h
 * @brief PRG-Image-Validierungsvertrag.
 *
 * Layer: Ring-0 process subsystem.
 * Contract: Erfolg liefert begrenzte, nicht überlappende Abbildungsdaten.
 * Safety: Fehler werden vor sichtbaren Seiteneffekten abgewiesen; Arbeit und Speicher sind begrenzt.
 */
#ifndef KERNEL_PROC_PROGRAM_IMAGE_H
#define KERNEL_PROC_PROGRAM_IMAGE_H

#include <stdint.h>

/* Validate a complete fixed-address MYPR v1 image before it is mapped. */
int program_image_validate(const void* image, uint32_t image_size,
                           uint32_t region_size);

#endif
