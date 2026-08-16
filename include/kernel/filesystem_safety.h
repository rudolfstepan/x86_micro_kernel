#ifndef KERNEL_FILESYSTEM_SAFETY_H
#define KERNEL_FILESYSTEM_SAFETY_H

#include <stdbool.h>
#include <stdint.h>

bool filesystem_safety_init(uint64_t now_ms);
bool filesystem_mutation_begin(uint64_t now_ms);
bool filesystem_mutation_end(bool commit);
void filesystem_fence_mutations(void);
bool filesystem_restore_mutations_after_recovery(void);
bool filesystem_is_read_only(void);

#endif
