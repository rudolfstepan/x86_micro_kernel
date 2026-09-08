#include <stdio.h>
#include <stdint.h>
#include <time.h>
#define JS_CASE_DETAIL(index,status,output) printf("JS_CASE index=%u status=%d output=[%s]\n",index,status,output)
#include "js_vectors.h"
#include "js_script_vectors.h"
#include "js_file_vectors.h"
int *reist_libc_errno(void) { static int value; return &value; }
static int monotonic(void *opaque,uint64_t *value) {
    (void)opaque;
    /* Host-only test source. Actual guest uses x86os_monotonic_ms. */
    *value=(uint64_t)clock()*1000/CLOCKS_PER_SEC; return 0;
}
int main(void) {
    uint16_t before,control=0x037f; uint32_t old_simd,simd=0x1f80;
    __asm__ volatile("fnstcw %0; stmxcsr %1; fldcw %2; ldmxcsr %3"
        : "=m"(before),"=m"(old_simd) : "m"(control),"m"(simd));
    int line=js_vectors(monotonic,NULL);
    if(!line) {
        line=js_script_vectors(monotonic,NULL);
        if(line) printf("JS_SCRIPT_HOST_FAIL line=%d\n",line);
        else puts("JS_SCRIPT_HOST_OK cases=11");
    }
    if(!line){line=js_file_vectors(monotonic,NULL);if(line)printf("JS_FILES_ENGINE_FAIL line=%d\n",line);else puts("JS_FILES_ENGINE_OK cases=15");}
    __asm__ volatile("fldcw %0; ldmxcsr %1" :: "m"(before),"m"(old_simd));
    if(line) { printf("JS_HOST_FAIL line=%d\n",line); return 1; }
    puts("JS_HOST_OK language=19 cycles=2000 source=1048576 failures=4 fresh=8 lrint=4");
    return 0;
}
