#include "arch/x86/include/fpu.h"
#include "arch/x86/include/cpu_local.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FPU_BOOT_FAIL line=%d\n",__LINE__); exit(1); } } while (0)
static x86_cpu_local_t local;
static uint32_t caps=(1U<<0)|(1U<<24)|(1U<<25), cr0=0x80010015U, cr4, flags;
static unsigned writes, saves, restores;
static int bad_readback, bad_reset;
static uint32_t mask=0xffbf;
static uint8_t hardware[512];
x86_cpu_local_t *x86_cpu_local_current(void) { return &local; }
uint32_t x86_fpu_test_features(void) { return caps; }
uint32_t x86_fpu_test_read(unsigned reg) {
    return reg==0 ? cr0 : reg==4 ? cr4 : flags;
}
void x86_fpu_test_write(unsigned reg, uint32_t value) {
    ++writes;
    if (!bad_readback) { if (!reg) cr0=value; else cr4=value; }
}
void x86_fpu_test_save(void *state) {
    ++saves; memcpy(state,hardware,512); ((uint32_t *)state)[7]=mask;
}
void x86_fpu_test_restore(const void *state) {
    ++restores; memcpy(hardware,state,512);
    if (bad_reset) hardware[160]=1;
}
int main(int argc, char **argv) {
    const uint32_t expected_mask=(argc==2 && !strcmp(argv[1],"amd-mm")) ? 0x2ffffU : 0xffbfU;
    mask=expected_mask;
    uint8_t state[528] __attribute__((aligned(16)));
    memset(state,0xa5,sizeof(state));
    CHECK(!x86_fpu_state_reset(NULL));
    CHECK(!x86_fpu_state_reset(state+1));
    for(unsigned i=0;i<528;++i) CHECK(state[i]==0xa5);
    CHECK(x86_fpu_state_reset(state));
    for(unsigned i=0;i<512;++i) {
        unsigned expected=i==0?0x7f:i==1?3:i==24?0x80:i==25?0x1f:0;
        CHECK(state[i]==expected);
    }
    for(unsigned i=512;i<528;++i) CHECK(state[i]==0xa5);
    local.registered=1;
    local.cpu_index=1;
    CHECK(!x86_fpu_initialize_cpu() && !writes); /* BSP first */
    local.cpu_index=16;
    CHECK(!x86_fpu_initialize_cpu() && !writes);
    local.cpu_index=0;
    uint32_t all=caps;
    for(unsigned bit=0;bit<32;++bit) if(all&(1U<<bit)) {
        caps=all&~(1U<<bit);
        CHECK(!x86_fpu_initialize_cpu() && !writes && !saves);
    }
    caps=0; CHECK(!x86_fpu_initialize_cpu() && !writes);
    caps=all; flags=1U<<9;
    CHECK(!x86_fpu_initialize_cpu() && !writes);
    flags=0; cr4=1U<<18;
    CHECK(!x86_fpu_initialize_cpu() && !writes);
    cr4=0; bad_readback=1;
    CHECK(!x86_fpu_initialize_cpu() && !saves && !x86_fpu_cpu_ready(0));
    bad_readback=0;
    mask=expected_mask|(1U<<16);
    CHECK(!x86_fpu_initialize_cpu() && !x86_fpu_cpu_ready(0) && x86_fpu_boot_stage(0)==5);
    mask=expected_mask&~(1U<<7);
    CHECK(!x86_fpu_initialize_cpu() && !x86_fpu_cpu_ready(0));
    mask=expected_mask; bad_reset=1;
    CHECK(!x86_fpu_initialize_cpu() && !x86_fpu_cpu_ready(0));
    bad_reset=0;
    CHECK(x86_fpu_initialize_cpu() && x86_fpu_cpu_ready(0));
    CHECK(x86_fpu_boot_stage(0)==0 && x86_fpu_mxcsr_mask(0)==expected_mask);
    CHECK(x86_fpu_boot_stage(16)==UINT32_MAX && x86_fpu_mxcsr_mask(16)==0);
    CHECK(cr0==0x80010033U && cr4==0x600U);
    unsigned prior=writes;
    CHECK(!x86_fpu_initialize_cpu() && writes==prior);
    local.cpu_index=1; mask=expected_mask^0x40U;
    CHECK(!x86_fpu_initialize_cpu() && !x86_fpu_cpu_ready(1));
    /* Zero reports the architectural fallback, not a universal CPU mask. */
    mask=0;
    if(expected_mask!=0xffbfU) {
        CHECK(!x86_fpu_initialize_cpu() && !x86_fpu_cpu_ready(1));
        mask=expected_mask;
    }
    for(unsigned cpu=1;cpu<16;++cpu) {
        local.cpu_index=cpu;
        CHECK(x86_fpu_initialize_cpu() && x86_fpu_cpu_ready(cpu));
    }
    CHECK(!x86_fpu_cpu_ready(16) && !x86_fpu_cpu_ready(UINT32_MAX));
    printf("FPU_BOOT_OK cpus=16 saves=%u restores=%u\n",saves,restores);
    return 0;
}
