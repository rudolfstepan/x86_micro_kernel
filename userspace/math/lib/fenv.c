/* Thread-owned Ring-3 x87/MXCSR only. Requires accepted R1.3 FPU profile. */
#include <fenv.h>
#include <stdint.h>

int fegetround(void) {
    uint16_t control;
    uint32_t mxcsr;
    __asm__ volatile("fnstcw %0; stmxcsr %1" : "=m"(control),"=m"(mxcsr));
    int mode=control&0xc00;
    return mode==(int)((mxcsr>>3)&0xc00) ? mode : -1;
}

int fesetround(int mode) {
    if (mode!=FE_TONEAREST && mode!=FE_DOWNWARD &&
        mode!=FE_UPWARD && mode!=FE_TOWARDZERO) return -1;
    uint16_t control;
    uint32_t mxcsr;
    __asm__ volatile("fnstcw %0; stmxcsr %1" : "=m"(control),"=m"(mxcsr));
    control=(uint16_t)((control&~0xc00U)|(unsigned)mode);
    mxcsr=(mxcsr&~0x6000U)|((unsigned)mode<<3);
    __asm__ volatile("fldcw %0; ldmxcsr %1" : : "m"(control),"m"(mxcsr) : "memory");
    return 0;
}

int fetestexcept(int exceptions) {
    uint16_t status;
    uint32_t mxcsr;
    __asm__ volatile("fnstsw %0; stmxcsr %1" : "=am"(status),"=m"(mxcsr));
    return (int)(status|mxcsr)&exceptions&FE_ALL_EXCEPT;
}

int feclearexcept(int exceptions) {
    uint32_t environment[7],mxcsr;
    uint32_t mask=(uint32_t)exceptions&FE_ALL_EXCEPT;
    if (!mask) return 0;
    /* FNSTENV masks x87 exceptions temporarily; restore all other fields and
     * original masks with FLDENV. No waiting instruction or heap/OS access. */
    __asm__ volatile("fnstenv %0; stmxcsr %1" : "=m"(environment),"=m"(mxcsr));
    environment[1]&=~mask;
    mxcsr&=~mask;
    __asm__ volatile("fldenv %0; ldmxcsr %1" : : "m"(environment),"m"(mxcsr) : "memory");
    return 0;
}
