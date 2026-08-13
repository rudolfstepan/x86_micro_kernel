#ifndef KERNEL_OUTPUT_FENCE_H
#define KERNEL_OUTPUT_FENCE_H

#include <stdbool.h>
#include <stdint.h>

#define OUTPUT_FENCE_MAX_HANDLERS 8U

typedef void (*output_fence_handler_t)(void);

void output_fence_init(void);
bool output_fence_register(output_fence_handler_t handler);
void output_fence_all(void);
bool output_fence_is_active(void);
uint32_t output_fence_handler_count(void);

#endif
