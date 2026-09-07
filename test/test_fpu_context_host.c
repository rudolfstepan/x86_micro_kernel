/* Executes the production i386 switch, including null-old exit. No kernel
 * instruction or host-library call while a fixture FP state is live. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t esp, ebp, ebx, esi, edi, eip;
    uint32_t pad[2];
    uint8_t fp[512];
} __attribute__((aligned(16))) context;
extern void swtch(context *, context *);
static context root, child;
static uint8_t stack[65536] __attribute__((aligned(16)));
static uint8_t original[512] __attribute__((aligned(16)));
static uint8_t a[512] __attribute__((aligned(16)));
static uint8_t b[512] __attribute__((aligned(16)));
static uint8_t observed[512] __attribute__((aligned(16)));
static unsigned errors, resumed;

static void save(void *p) { __asm__ volatile("fxsave (%0)" : : "r"(p) : "memory"); }
static void load(const void *p) { __asm__ volatile("fxrstor (%0)" : : "r"(p) : "memory"); }
static void pattern(uint8_t *p, unsigned seed) {
    for (unsigned i=0; i<512; ++i) p[i]=0;
    p[0]=0x7f; p[1]=(uint8_t)(3U | ((seed&3U)<<2)); p[4]=0xff;
    p[24]=0x80; p[25]=(uint8_t)(0x1fU | ((seed&3U)<<5));
    for (unsigned i=32; i<288; ++i) p[i]=(uint8_t)(i*17U+seed);
}
static int same(const uint8_t *p, const uint8_t *q) {
    for (unsigned i=0; i<5; ++i) if(p[i]!=q[i]) return 0;
    for (unsigned i=24; i<28; ++i) if(p[i]!=q[i]) return 0;
    for (unsigned reg=0; reg<8; ++reg)
        for (unsigned j=0; j<10; ++j)
            if(p[32+16*reg+j]!=q[32+16*reg+j]) return 0;
    for (unsigned i=160; i<288; ++i) if(p[i]!=q[i]) return 0;
    return 1;
}
static void child_entry(void) {
    for (unsigned i=0; i<128; ++i) {
        save(observed);
        if (!same(observed,b)) ++errors;
        pattern(b,202U+i); load(b);
        swtch(&child,&root);
        save(observed);
        if (!same(observed,b)) ++errors;
        ++resumed;
    }
    swtch(NULL,&root);
    __builtin_trap();
}
int main(void) {
    _Static_assert(sizeof(context)==544 && offsetof(context,fp)==32, "fixture layout");
    save(original);
    pattern(a,11); pattern(b,201);
    memcpy(child.fp,b,512);
    uintptr_t top=(uintptr_t)(stack+sizeof(stack));
    uint32_t *sp=(uint32_t *)(top-4);
    *--sp=(uint32_t)(uintptr_t)child_entry;
    child.esp=(uint32_t)(uintptr_t)sp;
    load(a);
    for (unsigned i=0; i<129; ++i) {
        swtch(&root,&child);
        save(observed);
        if (!same(observed,a)) ++errors;
        pattern(a,12U+i); load(a);
    }
    load(original);
    if (errors || resumed!=128) {
        fprintf(stderr,"FPU_SWITCH_FAIL mismatches=%u resumed=%u\n",errors,resumed);
        return 1;
    }
    puts("FPU_SWITCH_OK pairs=128 null_old=1 x87=8 xmm=8 controls=1");
    return 0;
}
