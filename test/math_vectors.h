/* Fixed binary64 oracle shared by host and the real SDK consumer.
 * Transcendental references are hexadecimal binary64 constants, <=4 ULP.
 * No host math, allocation, input or platform service in this fixture. */
#ifndef REIST_MATH_VECTORS_H
#define REIST_MATH_VECTORS_H
#include <math.h>
#include <fenv.h>
#include <stdint.h>
#include <float.h>
#include <limits.h>

static uint64_t math_bits(double x) {
    union { double d; uint64_t u; } bits={x}; return bits.u;
}
static int math_close(double a,double b,unsigned ulps) {
    if (isnan(a)||isnan(b)) return 0;
    uint64_t x=math_bits(a),y=math_bits(b);
    if (isinf(a)||isinf(b)) return x==y;
    if ((x>>63)!=(y>>63)) return 0;
    return (x>y ? x-y : y-x)<=ulps;
}
#define MATH_CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int math_vectors(void) {
    static const struct { double (*fn)(double); double input,expected; unsigned ulps; } values[]={
        {fabs,-3.5,3.5,0},{sqrt,2,0x1.6a09e667f3bcdp0,1},{cbrt,27,3,2},
        {floor,-1.5,-2,0},{ceil,-1.5,-1,0},{trunc,-1.5,-1,0},{round,-1.5,-2,0},
        {rint,2.5,2,0},{nearbyint,3.5,4,0},
        {sin,0.5,0x1.eaee8744b05f0p-2,4},{cos,0.5,0x1.c1528065b7d50p-1,4},
        {tan,0.5,0x1.17b4f5bf3474ap-1,4},{asin,0.5,0x1.0c152382d7366p-1,4},
        {acos,0.5,0x1.0c152382d7366p0,4},{atan,1,0x1.921fb54442d18p-1,4},
        {sinh,0.5,0x1.0acd00fe63b97p-1,4},{cosh,0.5,0x1.20ac1862ae8d0p0,4},
        {tanh,0.5,0x1.d9353d7568af3p-2,4},
        {asinh,0,0,0},{acosh,1,0,0},{atanh,0,0,0},
        {exp,1,0x1.5bf0a8b145769p1,4},{exp2,10,1024,0},
        {expm1,1,0x1.b7e151628aed2p0,4},{log,2,0x1.62e42fefa39efp-1,4},
        {log2,8,3,0},{log10,100,2,4},{log1p,1,0x1.62e42fefa39efp-1,4},
    };
    MATH_CHECK(fesetround(FE_TONEAREST)==0 && fegetround()==FE_TONEAREST);
    for (unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i)
        MATH_CHECK(math_close(values[i].fn(values[i].input),values[i].expected,values[i].ulps));
    MATH_CHECK(math_close(atan2(1,1),0x1.921fb54442d18p-1,4));
    MATH_CHECK(pow(2,10)==1024 && hypot(3,4)==5 && fmod(5.5,2)==1.5);
    MATH_CHECK(remainder(5.5,2)==-0.5);
    int exponent=0,quotient=0; double integer=0;
    MATH_CHECK(remquo(5.5,2,&quotient)==-0.5 && (quotient&7)==3);
    MATH_CHECK(frexp(12,&exponent)==0.75 && exponent==4);
    MATH_CHECK(ldexp(0.75,4)==12 && scalbn(0.75,4)==12 && scalbln(0.75,4)==12);
    MATH_CHECK(modf(-1.5,&integer)==-0.5 && integer==-1);
    MATH_CHECK(math_bits(copysign(0,-1))==UINT64_C(0x8000000000000000));
    MATH_CHECK(fmin(NAN,3)==3 && fmax(3,NAN)==3 && fdim(5,2)==3 && fdim(2,5)==0);
    MATH_CHECK(math_bits(nextafter(1,2))==UINT64_C(0x3ff0000000000001));
    MATH_CHECK(math_bits(nextafter(0,1))==1 && fpclassify(0x1p-1074)==FP_SUBNORMAL);
    MATH_CHECK(isfinite(DBL_MAX) && isnormal(DBL_MIN) && isinf(INFINITY) && isnan(NAN));
    MATH_CHECK(fpclassify(0.0)==FP_ZERO && signbit(-0.0) && !signbit(0.0));
    MATH_CHECK(isunordered(NAN,1.0) && !isgreater(NAN,1.0) && isless(1.0,2.0));
    MATH_CHECK(isgreaterequal(2.0,2.0) && islessequal(1.0,1.0) && islessgreater(1.0,2.0));
    MATH_CHECK(math_bits(sqrt(-0.0))==UINT64_C(0x8000000000000000));
    MATH_CHECK(math_bits(sin(-0.0))==UINT64_C(0x8000000000000000));
    MATH_CHECK(math_bits(trunc(-0.5))==UINT64_C(0x8000000000000000));
    MATH_CHECK(math_bits(fmod(-4,2))==UINT64_C(0x8000000000000000));
    MATH_CHECK(math_bits(scalbn(1,-1074))==1 && frexp(0x1p-1074,&exponent)==0.5 && exponent==-1073);
    MATH_CHECK(sqrt(INFINITY)==INFINITY && exp(-INFINITY)==0 && pow(NAN,0)==1);
    MATH_CHECK(isnan(sin(INFINITY)) && isnan(acos(2)) && isnan(fmod(1,0)));
    MATH_CHECK(feclearexcept(FE_ALL_EXCEPT)==0);
    volatile double negative=-1,zero=0,huge=DBL_MAX,tiny=DBL_MIN;
    MATH_CHECK(isnan(sqrt(negative)) && (fetestexcept(FE_INVALID)&FE_INVALID));
    MATH_CHECK(feclearexcept(FE_INVALID)==0 && !(fetestexcept(FE_INVALID)&FE_INVALID));
    MATH_CHECK(log(zero)==-INFINITY && (fetestexcept(FE_DIVBYZERO)&FE_DIVBYZERO));
    MATH_CHECK(isinf(scalbn(huge,1)) && (fetestexcept(FE_OVERFLOW)&FE_OVERFLOW));
    MATH_CHECK(scalbn(tiny,-100)==0 && (fetestexcept(FE_UNDERFLOW)&FE_UNDERFLOW));
    MATH_CHECK(fetestexcept(FE_ALL_EXCEPT)&FE_DIVBYZERO);
    MATH_CHECK(feclearexcept(FE_ALL_EXCEPT)==0 && fetestexcept(FE_ALL_EXCEPT)==0);
    /* Very large arguments exercise finite argument reduction, not a small-x surrogate. */
    MATH_CHECK(isfinite(sin(DBL_MAX)) && fabs(sin(DBL_MAX))<=1);
    MATH_CHECK(isfinite(cos(0x1p1000)) && fabs(cos(0x1p1000))<=1);
    MATH_CHECK(isinf(scalbn(1,INT_MAX)) && scalbn(1,INT_MIN)==0);
    MATH_CHECK(feclearexcept(FE_ALL_EXCEPT)==0);
    return 0;
}

static int math_environment(void) {
    static const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    static const double positive[]={2,1,2,1},negative[]={-2,-2,-1,-1};
    uint16_t original,original_status; uint32_t original_simd;
    __asm__ volatile("fnstcw %0; fnstsw %1; stmxcsr %2" : "=m"(original),"=am"(original_status),"=m"(original_simd));
    for (unsigned i=0;i<4;++i) {
        MATH_CHECK(fesetround(modes[i])==0 && fegetround()==modes[i]);
        uint16_t valid_control; uint32_t valid_simd;
        __asm__ volatile("fnstcw %0; stmxcsr %1" : "=m"(valid_control),"=m"(valid_simd));
        MATH_CHECK((valid_control&~0xc00)==(original&~0xc00) &&
                   (valid_simd&~0x6000U)==(original_simd&~0x6000U));
        volatile double p=1.5,n=-1.5;
        MATH_CHECK(rint(p)==positive[i] && rint(n)==negative[i]);
        MATH_CHECK(feclearexcept(FE_ALL_EXCEPT)==0);
        MATH_CHECK(nearbyint(p)==positive[i] && fetestexcept(FE_INEXACT)==0);
        uint16_t control; uint32_t mxcsr;
        __asm__ volatile("fnstcw %0; stmxcsr %1" : "=m"(control),"=m"(mxcsr));
        for (int invalid=-1;invalid<=4096;++invalid) {
            if (invalid==FE_TONEAREST || invalid==FE_DOWNWARD || invalid==FE_UPWARD || invalid==FE_TOWARDZERO) continue;
            MATH_CHECK(fesetround(invalid)!=0);
            uint16_t now; uint32_t simd;
            __asm__ volatile("fnstcw %0; stmxcsr %1" : "=m"(now),"=m"(simd));
            MATH_CHECK(now==control && simd==mxcsr);
        }
        MATH_CHECK(fesetround(INT_MIN)!=0 && fesetround(INT_MAX)!=0 && fegetround()==modes[i]);
    }
    /* Seed all sticky flags in both units, then exhaustively clear subsets.
     * The non-standard denormal flag must remain untouched. Exceptions masked. */
    for(unsigned mask=0;mask<64;++mask) {
        uint32_t environment[7],simd;
        __asm__ volatile("fnstenv %0; stmxcsr %1" : "=m"(environment),"=m"(simd));
        environment[1]=(environment[1]&~0x3fU)|0x3fU; simd|=0x3fU;
        __asm__ volatile("fldenv %0; ldmxcsr %1" : : "m"(environment),"m"(simd) : "memory");
        MATH_CHECK(feclearexcept((int)mask)==0 && fetestexcept(FE_ALL_EXCEPT)==(int)(FE_ALL_EXCEPT&~mask));
        uint16_t status;
        __asm__ volatile("fnstsw %0; stmxcsr %1" : "=am"(status),"=m"(simd));
        MATH_CHECK((status&0x3f)==(0x3fU&~(mask&FE_ALL_EXCEPT)) &&
                   (simd&0x3f)==(0x3fU&~(mask&FE_ALL_EXCEPT)));
    }
    uint32_t restored[7];
    __asm__ volatile("fnstenv %0" : "=m"(restored));
    restored[1]=(restored[1]&~0x3fU)|(original_status&0x3fU);
    __asm__ volatile("fldenv %0; ldmxcsr %1" : : "m"(restored),"m"(original_simd) : "memory");
    MATH_CHECK(fesetround(FE_TONEAREST)==0 && feclearexcept(FE_ALL_EXCEPT)==0);
    return 0;
}
#undef MATH_CHECK
#endif
