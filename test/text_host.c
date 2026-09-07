#include <stdio.h>
#include <stdint.h>
#include <string.h>
/* Host-only storage counterpart; target links the real process libc. */
int *reist_libc_errno(void) { static int value; return &value; }
/* Host reference uses the toolchain's conventional C99 snprintf, not candidate. */
static int reference(char *out,size_t n,int value,double real) {
    return snprintf(out,n,"[%+08d][%.8f][%.6e][%.10g]",value,real,real,real);
}
int reist_text_snprintf(char *,size_t,const char *,...);
int reist_text_vsnprintf(char *,size_t,const char *,va_list);
#define snprintf reist_text_snprintf
#define vsnprintf reist_text_vsnprintf
#define TEXT_FAILURE(line,buffer) (printf("TEXT_DETAIL line=%d output=[%s]\n",line,buffer),(line))
#include "text_vectors.h"

int main(void) {
    uint16_t before,control=0x037f; uint32_t simd_before,simd=0x1f80;
    __asm__ volatile("fnstcw %0; stmxcsr %1; fldcw %2; ldmxcsr %3"
        : "=m"(before),"=m"(simd_before) : "m"(control),"m"(simd));
    int result=text_vectors();
    if(!result) for(int i=0;i<128;++i) {
        char actual[160],expected[160]; double d=(i-64)*1.23456789;
        int a=snprintf(actual,sizeof actual,"[%+08d][%.8f][%.6e][%.10g]",i-64,d,d,d);
        int b=reference(expected,sizeof expected,i-64,d);
        if(a!=b || strcmp(actual,expected)) { result=1000+i; break; }
    }
    __asm__ volatile("fldcw %0; ldmxcsr %1" :: "m"(before),"m"(simd_before));
    if(result) { printf("TEXT_HOST_FAIL vector=%d\n",result); return 1; }
    puts("TEXT_HOST_OK reference_samples=128"); return 0;
}
