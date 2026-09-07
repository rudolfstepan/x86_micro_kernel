/* Intel SDM legacy FXSAVE profile. No lazy owner, allocation, I/O or waits. */
#include "arch/x86/include/fpu.h"
#include "arch/x86/include/cpu_local.h"
#include <stddef.h>

#define REQUIRED_FEATURES ((1U<<0) | (1U<<24) | (1U<<25))
#define CR0_REQUIRED ((1U<<1) | (1U<<5))
#define CR0_FORBIDDEN ((1U<<2) | (1U<<3))
#define CR4_REQUIRED ((1U<<9) | (1U<<10))
#define CR4_OSXSAVE (1U<<18)
static uint32_t ready[X86_CPU_LOCAL_MAX];
static uint32_t masks[X86_CPU_LOCAL_MAX];
static uint32_t boot_stage[X86_CPU_LOCAL_MAX];

#ifdef REIST_HOST_TEST
extern uint32_t x86_fpu_test_features(void);
extern uint32_t x86_fpu_test_read(unsigned reg);
extern void x86_fpu_test_write(unsigned reg, uint32_t value);
extern void x86_fpu_test_save(void *state);
extern void x86_fpu_test_restore(const void *state);
#define features x86_fpu_test_features
#define read_control x86_fpu_test_read
#define write_control x86_fpu_test_write
#define save_state x86_fpu_test_save
#define restore_state x86_fpu_test_restore
#else
static uint32_t features(void) {
    uint32_t original, changed;
    __asm__ volatile("pushf; pop %0" : "=r"(original));
    uint32_t toggled=original^(1U<<21);
    __asm__ volatile("push %0; popf; pushf; pop %1" : "+r"(toggled), "=r"(changed) : : "cc", "memory");
    __asm__ volatile("push %0; popf" : : "r"(original) : "cc", "memory");
    if (!((original^changed)&(1U<<21))) return 0;
    uint32_t a=0,b,c,d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    if (a<1) return 0;
    a=1;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    return d;
}
static uint32_t read_control(unsigned reg) {
    uint32_t value;
    if (reg==0) __asm__ volatile("mov %%cr0,%0" : "=r"(value));
    else if (reg==4) __asm__ volatile("mov %%cr4,%0" : "=r"(value));
    else __asm__ volatile("pushf; pop %0" : "=r"(value));
    return value;
}
static void write_control(unsigned reg, uint32_t value) {
    if (reg==0) __asm__ volatile("mov %0,%%cr0" : : "r"(value) : "memory");
    else __asm__ volatile("mov %0,%%cr4" : : "r"(value) : "memory");
}
static void save_state(void *state) {
    __asm__ volatile("fxsave (%0)" : : "r"(state) : "memory");
}
static void restore_state(const void *state) {
    __asm__ volatile("fxrstor (%0)" : : "r"(state) : "memory");
}
#endif

bool x86_fpu_state_reset(void *state) {
    if (state==NULL || ((uintptr_t)state&15U)) return false;
    uint32_t *words=state;
    for (unsigned i=0; i<X86_FPU_STATE_BYTES/4; ++i) words[i]=0;
    words[0]=0x037fU; /* masked exceptions, extended precision, nearest */
    words[6]=0x1f80U; /* masked SIMD exceptions, nearest, no DAZ/FTZ */
    return true;
}

bool x86_fpu_cpu_ready(uint32_t cpu) {
    return cpu<X86_CPU_LOCAL_MAX &&
        __atomic_load_n(&ready[cpu],__ATOMIC_ACQUIRE)!=0U;
}

uint32_t x86_fpu_boot_stage(uint32_t cpu) {
    return cpu<X86_CPU_LOCAL_MAX ? __atomic_load_n(&boot_stage[cpu],__ATOMIC_ACQUIRE) : UINT32_MAX;
}

uint32_t x86_fpu_mxcsr_mask(uint32_t cpu) {
    return cpu<X86_CPU_LOCAL_MAX ? masks[cpu] : 0U;
}

bool x86_fpu_initialize_cpu(void) {
    x86_cpu_local_t *local=x86_cpu_local_current();
    if (local==NULL || local->cpu_index>=X86_CPU_LOCAL_MAX ||
        !local->registered || (read_control(9)&(1U<<9))) return false;
    uint32_t cpu=local->cpu_index;
    if (x86_fpu_cpu_ready(cpu) || (cpu && !x86_fpu_cpu_ready(0))) return false;
    boot_stage[cpu]=1;
    if ((features()&REQUIRED_FEATURES)!=REQUIRED_FEATURES) return false;
    uint32_t cr0=read_control(0), cr4=read_control(4);
    /* Never disable an already active unknown extended-state domain. */
    boot_stage[cpu]=2;
    if (cr4&CR4_OSXSAVE) return false;
    cr0=(cr0|CR0_REQUIRED)&~CR0_FORBIDDEN;
    cr4|=CR4_REQUIRED;
    write_control(0,cr0);
    write_control(4,cr4);
    boot_stage[cpu]=3;
    if (read_control(0)!=cr0 || read_control(4)!=cr4) return false;

    _Static_assert(X86_SCHEDULER_CONTEXT_WORDS*4==544, "idle switch layout");
    _Static_assert(offsetof(x86_cpu_local_t,scheduler_context)%16==0, "idle alignment");
    uint32_t *state=&local->scheduler_context[X86_FPU_CONTEXT_OFFSET/4];
    boot_stage[cpu]=4;
    if (!x86_fpu_state_reset(state)) return false;
    save_state(state);
    uint32_t mask=state[7] ? state[7] : 0xffbfU;
    masks[cpu]=mask;
    boot_stage[cpu]=5;
    /* AMD APM: bit17 is the legacy SSE Misaligned Exception Mask.
     * It is preserved by FXSAVE and stays clear in every fresh context. */
    if ((mask&~0x2ffffU) || (mask&0x1f80U)!=0x1f80U ||
        (cpu && mask!=masks[0])) return false;
    (void)x86_fpu_state_reset(state);
    restore_state(state);
    save_state(state);
    boot_stage[cpu]=6;
    if (state[0]!=0x037fU || (state[1]&0xffU) || state[6]!=0x1f80U)
        return false;
    for (unsigned i=32; i<288; ++i) {
        /* FXSAVE need not write the six reserved bytes in each x87 slot. */
        if (i<160 && (i-32)%16>=10) continue;
        if (((uint8_t *)state)[i]) { boot_stage[cpu]=1000U+i; return false; }
    }
    boot_stage[cpu]=0;
    __atomic_store_n(&ready[cpu],1U,__ATOMIC_RELEASE);
    return true;
}
