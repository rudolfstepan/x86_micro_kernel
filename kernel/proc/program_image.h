#ifndef KERNEL_PROC_PROGRAM_IMAGE_H
#define KERNEL_PROC_PROGRAM_IMAGE_H

#include <stdint.h>

/* Validate a complete MYPR image before it is executed or relocated. */
int program_image_validate(const void* image, uint32_t image_size,
                           uint32_t region_size);

#endif
