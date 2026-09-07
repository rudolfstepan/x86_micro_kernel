#ifndef REIST_TEXT_VECTORS_H
#define REIST_TEXT_VECTORS_H
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <float.h>
#include <stdarg.h>
#include <errno.h>
#include <math.h>
#include <fenv.h>
#include <string.h>

#ifndef TEXT_FAILURE
#define TEXT_FAILURE(line,buffer) (line)
#endif
#define TC(x) do { if (!(x)) return TEXT_FAILURE(__LINE__,out); } while (0)
static int text_vcall(char *out, size_t capacity, const char *format, ...) {
    va_list ap; va_start(ap,format);
    int n=vsnprintf(out,capacity,format,ap);
    va_end(ap); return n;
}
static int text_vectors(void) {
    char out[256];
    TC(snprintf(out,sizeof out,"%d %i %u %o %#x %#X",INT_MIN,-7,UINT_MAX,9,42,42)==38);
    TC(!strcmp(out,"-2147483648 -7 4294967295 11 0x2a 0X2A"));
    TC(snprintf(out,sizeof out,"%hhd %hhu %hd %hu",-128,255,-32768,65535)==21);
    TC(!strcmp(out,"-128 255 -32768 65535"));
    TC(snprintf(out,sizeof out,"%ld %lu %lld %llu",LONG_MIN,ULONG_MAX,LLONG_MIN,ULLONG_MAX)>0);
    TC(!strcmp(out,"-2147483648 4294967295 -9223372036854775808 18446744073709551615"));
    TC(snprintf(out,sizeof out,"%jd %ju %zd %zu %td",(intmax_t)-9,(uintmax_t)10,(ptrdiff_t)-11,(size_t)12,(ptrdiff_t)-13)==16);
    TC(!strcmp(out,"-9 10 -11 12 -13"));
    TC(snprintf(out,sizeof out,"[%+08d][%-6s][%#o][%.0u]",42,"abc",8,0)==25);
    TC(!strcmp(out,"[+0000042][abc   ][010][]"));
    TC(text_vcall(out,sizeof out,"%*.*s/%%/%c",-6,3,"abcdef",'!')==10);
    TC(!strcmp(out,"abc   /%/!"));
    /* C leaves pointer spelling implementation-defined; pinned musl uses hex. */
    TC(snprintf(out,sizeof out,"%p",(void *)(uintptr_t)0x1234)==6);
    TC(!strcmp(out,"0x1234"));
    TC(snprintf(out,sizeof out,"%.2f %.3e %.6g %a",1.25,1.25,1.25,1.5)>0);
    TC(!strcmp(out,"1.25 1.250e+00 1.25 0x1.8p+0"));
    TC(snprintf(out,sizeof out,"%.1F %.1E %.2G %A",1.25,1.25,1.25,1.5)>0);
    TC(!strcmp(out,"1.2 1.2E+00 1.2 0X1.8P+0"));
    TC(snprintf(out,sizeof out,"%.18Lf %.0Lf",1.000000000000000001L,0x1p64L)>0);
    TC(!strcmp(out,"1.000000000000000001 18446744073709551616"));
    TC(snprintf(out,sizeof out,"%f %g %G",-0.0,INFINITY,NAN)>0);
    TC(!strcmp(out,"-0.000000 inf NAN"));
    TC(snprintf(out,sizeof out,"%.17g %.17g",DBL_MAX,0x1p-1074)>0);
    TC(!strcmp(out,"1.7976931348623157e+308 4.9406564584124654e-324"));
    const int rounds[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    const char *rounded[]={"2 -2","2 -3","3 -2","2 -2"};
    for (unsigned i=0;i<4;++i) {
        TC(!fesetround(rounds[i])); TC(snprintf(out,sizeof out,"%.0f %.0f",2.5,-2.5)==4);
        TC(!strcmp(out,rounded[i])); TC(fegetround()==rounds[i]);
    }
    TC(!fesetround(FE_TONEAREST));
    int count=-1; short sc=-1; signed char cc=-1; long lc=-1;
    long long llc=-1; intmax_t jc=-1; ptrdiff_t tc=-1,zc=99;
    TC(snprintf(out,4,"abcdef%n%hn%hhn%ln%lln%jn%tn%zn",&count,&sc,&cc,&lc,&llc,&jc,&tc,&zc)==6);
    TC(!strcmp(out,"abc") && count==6 && sc==6 && cc==6 && lc==6 && llc==6 && jc==6 && tc==6 && zc==6);
    TC(snprintf(NULL,0,"%s/%d","abcdef",42)==9);
    for (size_t n=0;n<12;++n) {
        unsigned char guard[16]; memset(guard,0x5a,sizeof guard);
        TC(snprintf((char *)guard+2,n,"abcdefgh")==8);
        TC(guard[0]==0x5a && guard[1]==0x5a);
        if(n) { size_t used=n-1<8?n-1:8; TC(!memcmp(guard+2,"abcdefgh",used)); TC(guard[2+used]==0); }
        for(size_t i=2+n;i<sizeof guard;++i) TC(guard[i]==0x5a);
    }
    const wchar_t wide[]={'A','z',0};
    TC(snprintf(out,sizeof out,"%lc/%.1ls",'Z',wide)==3 && !strcmp(out,"Z/A"));
    errno=0; TC(snprintf(out,sizeof out,"%lc",0x100)<0 && errno==EILSEQ);
    errno=0; TC(text_vcall(out,sizeof out,"%")<0 && errno==EINVAL);
    errno=0; TC(text_vcall(out,sizeof out,"%2147483648d",1)<0 && errno==EOVERFLOW);
    errno=0; TC(text_vcall(out,sizeof out,"%*d",INT_MIN,1)<0 && errno==EOVERFLOW);
    TC(text_vcall(out,4,"%*s",INT_MAX,"")==INT_MAX && !strcmp(out,"   "));
    TC(text_vcall(out,4,"%-*s",INT_MAX,"x")==INT_MAX && !strcmp(out,"x  "));
    errno=0; TC(text_vcall(out,4,"%*s!",INT_MAX,"")<0 && errno==EOVERFLOW);
    /* Precision/count overflow must not overflow intermediate signed math. */
    errno=0; TC(text_vcall(out,4,"%.*f",INT_MAX,1.0)<0 && errno==EOVERFLOW);
    errno=0; TC(text_vcall(out,4,"%.*e",INT_MAX,0x1p-1074)<0 && errno==EOVERFLOW);
    TC(text_vcall(out,4,"%.*g",INT_MAX,0x1p-1074)>0);
    TC(!strcmp(out,"4.9"));
    TC(text_vcall(out,4,"%.*f",INT_MAX-2,1.0)==INT_MAX && !strcmp(out,"1.0"));
    TC(text_vcall(out,4,"%.*e",INT_MAX-6,1.0)==INT_MAX && !strcmp(out,"1.0"));
    TC(text_vcall(out,sizeof out,"%2$d/%1$d",10,20)==5 && !strcmp(out,"20/10"));
    /* Resource lengths are not silently limited to a tiny staging buffer. */
    static char large[1024*1024+1];
    memset(large,'x',sizeof large-1); large[sizeof large-1]=0;
    TC(snprintf(out,sizeof out,"%s",large)==1024*1024);
    TC(out[sizeof out-1]==0 && out[sizeof out-2]=='x');
    TC(snprintf(large,sizeof large,"%01048576d",7)==1024*1024);
    TC(large[0]=='0' && large[1024*1024-1]=='7' && large[1024*1024]==0);
    errno=61; TC(snprintf(out,sizeof out,"get %s/%u/\\u%04x","property",7U,0x100U)==21);
    TC(!strcmp(out,"get property/7/\\u0100") && errno==61);
    return 0;
}
#undef TC
#endif
