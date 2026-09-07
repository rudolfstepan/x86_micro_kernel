#ifndef REIST_MATH_PROFILE1_H
#define REIST_MATH_PROFILE1_H
#include <float.h>
#if FLT_EVAL_METHOD == 2
typedef long double float_t;
typedef long double double_t;
#elif FLT_EVAL_METHOD == 1
typedef double float_t;
typedef double double_t;
#else
typedef float float_t;
typedef double double_t;
#endif
#define HUGE_VAL (__builtin_huge_val())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define MATH_ERRNO 1
#define MATH_ERREXCEPT 2
#define math_errhandling MATH_ERREXCEPT
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define fpclassify(x) __builtin_fpclassify(FP_NAN,FP_INFINITE,FP_NORMAL,FP_SUBNORMAL,FP_ZERO,(x))
#define isfinite(x) __builtin_isfinite(x)
#define isinf(x) __builtin_isinf(x)
#define isnan(x) __builtin_isnan(x)
#define isnormal(x) __builtin_isnormal(x)
#define signbit(x) __builtin_signbit(x)
#define isgreater(x,y) __builtin_isgreater((x),(y))
#define isgreaterequal(x,y) __builtin_isgreaterequal((x),(y))
#define isless(x,y) __builtin_isless((x),(y))
#define islessequal(x,y) __builtin_islessequal((x),(y))
#define islessgreater(x,y) __builtin_islessgreater((x),(y))
#define isunordered(x,y) __builtin_isunordered((x),(y))
#ifdef __cplusplus
extern "C" {
#endif
#ifdef REIST_MATH_BUILD_INTERNAL
__attribute__((visibility("hidden"))) long double reist_math_internal_sqrtl(long double);
#endif
double fabs(double);
double sqrt(double);
double cbrt(double);
double floor(double);
double ceil(double);
double trunc(double);
double round(double);
double rint(double);
double nearbyint(double);
double sin(double);
double cos(double);
double tan(double);
double asin(double);
double acos(double);
double atan(double);
double atan2(double,double);
double sinh(double);
double cosh(double);
double tanh(double);
double asinh(double);
double acosh(double);
double atanh(double);
double exp(double);
double exp2(double);
double expm1(double);
double log(double);
double log2(double);
double log10(double);
double log1p(double);
double pow(double,double);
double hypot(double,double);
double fmod(double,double);
double remainder(double,double);
double remquo(double,double,int *);
double frexp(double,int *);
double ldexp(double,int);
double scalbn(double,int);
double scalbln(double,long);
double modf(double,double *);
double copysign(double,double);
double fmin(double,double);
double fmax(double,double);
double fdim(double,double);
double nextafter(double,double);
#ifdef __cplusplus
}
#endif
#endif
