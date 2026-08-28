#include "bootstrap_core.h"

#define REIST_HIGHER_HALF_BASE 0xFFFFFFFF80000000ULL
#define REIST_DIRECT_MAP_BASE 0xFFFF800000000000ULL
#define REIST_MANAGED_MEMORY_LIMIT 0x04000000ULL
#define REIST_ELF_PROBE_BASE 0x00400000ULL
#define REIST_ELF_PROBE_LIMIT 0x00408000ULL
#define REIST_PAGE_SIZE 4096ULL
#define REIST_FIXED_CAPACITY 4U
#define REIST_SYSCALL_ABI_VERSION 1U
#define REIST_COPY_BOUND REIST_X86_64_HANDOFF_SIZE
#define REIST_STATE_WORDS 4U

extern reist_u8 x86_64_c_handoff[];
extern reist_u32 x86_64_c_serial_write64(const char *message,
                                         reist_u64 length);

volatile reist_u64 x86_64_c_data_state[REIST_STATE_WORDS] = {
    0x5245495354434441ULL,
    0x1122334455667788ULL,
    0x8877665544332211ULL,
    0xA55AA55AA55AA55AULL,
};
volatile reist_u64 x86_64_c_bss_state[REIST_STATE_WORDS];

static const char c_callback_message[] = "REIST_X86_64_C_CALLBACK_OK\r\n";

static int canonical_kernel_address(reist_u64 value)
{
    return value >= REIST_HIGHER_HALF_BASE &&
           (value >> 48) == 0xFFFFULL;
}

static void zero_bytes(volatile reist_u8 *destination, reist_u32 count)
{
    reist_u32 index;

    for (index = 0U; index < count; ++index) {
        destination[index] = 0U;
    }
}

static void clear_owned_state(void)
{
    reist_u32 index;

    for (index = 0U; index < REIST_STATE_WORDS; ++index) {
        x86_64_c_data_state[index] = 0ULL;
        x86_64_c_bss_state[index] = 0ULL;
    }
}

static int validate_handoff(
    const volatile struct reist_x86_64_bootstrap_handoff_v1 *handoff)
{
    reist_u32 index;

    if (handoff->version != REIST_X86_64_HANDOFF_VERSION ||
        handoff->size != REIST_X86_64_HANDOFF_SIZE ||
        handoff->flags != REIST_X86_64_HANDOFF_REQUIRED_FLAGS ||
        handoff->higher_half_base != REIST_HIGHER_HALF_BASE ||
        handoff->direct_map_base != REIST_DIRECT_MAP_BASE ||
        handoff->managed_memory_limit != REIST_MANAGED_MEMORY_LIMIT ||
        handoff->elf_probe_base != REIST_ELF_PROBE_BASE ||
        handoff->elf_probe_limit != REIST_ELF_PROBE_LIMIT ||
        handoff->lifecycle_capabilities != REIST_X86_64_LIFECYCLE_CAPS ||
        handoff->task_capacity != REIST_FIXED_CAPACITY ||
        handoff->runqueue_capacity != REIST_FIXED_CAPACITY ||
        handoff->deadline_capacity != REIST_FIXED_CAPACITY ||
        handoff->syscall_abi_version != REIST_SYSCALL_ABI_VERSION) {
        return 0;
    }
    if ((handoff->kernel_cr3 & (REIST_PAGE_SIZE - 1ULL)) != 0ULL ||
        handoff->kernel_cr3 == 0ULL ||
        handoff->kernel_cr3 >= handoff->managed_memory_limit ||
        handoff->kernel_pml4 !=
            handoff->higher_half_base + handoff->kernel_cr3) {
        return 0;
    }
    if (!canonical_kernel_address(handoff->kernel_pml4) ||
        !canonical_kernel_address(handoff->kernel_text_start) ||
        !canonical_kernel_address(handoff->kernel_text_end) ||
        handoff->kernel_text_start >= handoff->kernel_text_end ||
        (handoff->kernel_text_start & (REIST_PAGE_SIZE - 1ULL)) != 0ULL ||
        (handoff->kernel_text_end & (REIST_PAGE_SIZE - 1ULL)) != 0ULL) {
        return 0;
    }
    if (handoff->elf_probe_base >= handoff->elf_probe_limit ||
        handoff->elf_probe_limit - handoff->elf_probe_base != 8ULL * REIST_PAGE_SIZE ||
        (handoff->elf_probe_base & (REIST_PAGE_SIZE - 1ULL)) != 0ULL ||
        (handoff->elf_probe_limit & (REIST_PAGE_SIZE - 1ULL)) != 0ULL ||
        handoff->managed_memory_limit / REIST_PAGE_SIZE != 16384ULL) {
        return 0;
    }
    for (index = 0U; index < sizeof(handoff->reserved); ++index) {
        if (handoff->reserved[index] != 0U) {
            return 0;
        }
    }
    return 1;
}

reist_u32 x86_64_c_core_entry(
    volatile struct reist_x86_64_bootstrap_handoff_v1 *handoff)
{
    volatile reist_u8 snapshot[REIST_COPY_BOUND];
    const volatile reist_u8 *source;
    reist_u64 arithmetic_proof;
    reist_u32 index;
    reist_u32 result = 0U;

    if ((volatile reist_u8 *)handoff != x86_64_c_handoff) {
        return 0U;
    }
    if (!validate_handoff(handoff)) {
        zero_bytes((volatile reist_u8 *)handoff, REIST_COPY_BOUND);
        return 0U;
    }

    source = (const volatile reist_u8 *)handoff;
    for (index = 0U; index < REIST_COPY_BOUND; ++index) {
        snapshot[index] = source[index];
    }
    for (index = 0U; index < REIST_COPY_BOUND; ++index) {
        if (snapshot[index] != source[index]) {
            goto cleanup;
        }
    }

    if (x86_64_c_data_state[0] != 0x5245495354434441ULL ||
        x86_64_c_data_state[1] != 0x1122334455667788ULL ||
        x86_64_c_data_state[2] != 0x8877665544332211ULL ||
        x86_64_c_data_state[3] != 0xA55AA55AA55AA55AULL) {
        goto cleanup;
    }
    for (index = 0U; index < REIST_STATE_WORDS; ++index) {
        if (x86_64_c_bss_state[index] != 0ULL) {
            goto cleanup;
        }
    }

    arithmetic_proof =
        (handoff->managed_memory_limit / REIST_PAGE_SIZE) +
        ((handoff->elf_probe_limit - handoff->elf_probe_base) /
         REIST_PAGE_SIZE) +
        (reist_u64)handoff->task_capacity +
        (reist_u64)handoff->runqueue_capacity +
        (reist_u64)handoff->deadline_capacity;
    if (arithmetic_proof != 16404ULL) {
        goto cleanup;
    }
    x86_64_c_bss_state[0] = arithmetic_proof;
    x86_64_c_bss_state[1] = handoff->kernel_cr3;
    x86_64_c_bss_state[2] = handoff->kernel_text_end -
                             handoff->kernel_text_start;
    x86_64_c_bss_state[3] = handoff->lifecycle_capabilities;
    if (x86_64_c_serial_write64(c_callback_message,
                                sizeof(c_callback_message) - 1U) != 1U) {
        goto cleanup;
    }
    result = 1U;

cleanup:
    clear_owned_state();
    zero_bytes((volatile reist_u8 *)handoff, REIST_COPY_BOUND);
    return result;
}
