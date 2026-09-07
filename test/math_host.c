/* Host stdout/DLL math are not linked to the prefixed candidate symbols. */
#include <stdio.h>
#include <windows.h>
#include "math_vectors.h"

static int compare_reference(void) {
    HMODULE reference=LoadLibraryExA("msvcrt.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!reference) return 1;
    static const struct { const char *name; double (*candidate)(double); } functions[]={
        {"sin",sin},{"cos",cos},{"atan",atan},{"exp",exp},{"log",log},{"sqrt",sqrt}};
    int failed=0;
    for (unsigned f=0;f<sizeof(functions)/sizeof(functions[0]);++f) {
        union { FARPROC address; double (*function)(double); } entry;
        entry.address=GetProcAddress(reference,functions[f].name);
        if (!entry.address) { failed=2; break; }
        for (unsigned i=1;i<=128;++i) {
            double x=(double)i/16.0;
            if (!math_close(functions[f].candidate(x),entry.function(x),4)) { failed=3+(int)f; break; }
        }
        if (failed) break;
    }
    FreeLibrary(reference);
    return failed;
}

int main(void) {
    uint16_t original,standard=0x037f; uint32_t simd,defaults=0x1f80;
    __asm__ volatile("fnstcw %0; stmxcsr %1" : "=m"(original),"=m"(simd));
    __asm__ volatile("fldcw %0; ldmxcsr %1" : : "m"(standard),"m"(defaults) : "memory");
    int vectors=math_vectors(),environment=math_environment(),reference=compare_reference();
    feclearexcept(FE_ALL_EXCEPT);
    __asm__ volatile("fldcw %0; ldmxcsr %1" : : "m"(original),"m"(simd) : "memory");
    if (vectors || environment || reference) {
        fprintf(stderr,"MATH_HOST_FAIL vectors_line=%d environment_line=%d reference=%d\n",vectors,environment,reference);
        return 1;
    }
    puts("MATH_HOST_OK functions=44 rounding=4 reference_samples=768");
    return 0;
}
