#ifndef KERNEL_STORAGE_SAFETY_H
#define KERNEL_STORAGE_SAFETY_H

#include <stdbool.h>
#include <stdint.h>

bool storage_safety_init(uint64_t now_ms);
bool storage_write_begin(uint64_t now_ms);
bool storage_write_end(void);
void storage_fence_writes(void);
bool storage_writes_fenced(void);

#endif
