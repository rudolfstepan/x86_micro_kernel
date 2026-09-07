#ifndef REIST_FENV_PROFILE1_H
#define REIST_FENV_PROFILE1_H
/* ISO C subset; values follow the i386 x87 representation. No trap-enable API. */
#define FE_INVALID 0x01
#define FE_DIVBYZERO 0x04
#define FE_OVERFLOW 0x08
#define FE_UNDERFLOW 0x10
#define FE_INEXACT 0x20
#define FE_ALL_EXCEPT 0x3d
#define FE_TONEAREST 0
#define FE_DOWNWARD 0x400
#define FE_UPWARD 0x800
#define FE_TOWARDZERO 0xc00
#ifdef __cplusplus
extern "C" {
#endif
int fegetround(void);
int fesetround(int);
int feclearexcept(int);
int fetestexcept(int);
#ifdef __cplusplus
}
#endif
#endif
