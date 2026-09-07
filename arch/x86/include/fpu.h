#ifndef REIST_X86_FPU_H
#define REIST_X86_FPU_H
#include <stdbool.h>
#include <stdint.h>

#define X86_FPU_STATE_BYTES 512U
#define X86_FPU_CONTEXT_OFFSET 32U
/* Kernel-owned, 16-byte aligned storage only. No user restore interface. */
bool x86_fpu_state_reset(void *state);
/* Once, IRQ-disabled, BSP first; AP before ONLINE. Failure denies admission. */
bool x86_fpu_initialize_cpu(void);
bool x86_fpu_cpu_ready(uint32_t cpu);
/* Bounded boot-only diagnostic; zero after success, UINT32_MAX if unknown. */
uint32_t x86_fpu_boot_stage(uint32_t cpu);
uint32_t x86_fpu_mxcsr_mask(uint32_t cpu);
#endif
