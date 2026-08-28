#ifndef REIST_X86_64_BOOTSTRAP_CORE_H
#define REIST_X86_64_BOOTSTRAP_CORE_H

typedef unsigned char reist_u8;
typedef unsigned int reist_u32;
typedef unsigned long long reist_u64;

#define REIST_X86_64_HANDOFF_VERSION 1U
#define REIST_X86_64_HANDOFF_SIZE 128U

#define REIST_X86_64_HANDOFF_LONG_MODE       (1ULL << 0)
#define REIST_X86_64_HANDOFF_HIGHER_HALF     (1ULL << 1)
#define REIST_X86_64_HANDOFF_NX              (1ULL << 2)
#define REIST_X86_64_HANDOFF_WRITE_PROTECT   (1ULL << 3)
#define REIST_X86_64_HANDOFF_PHYSICAL_MEMORY (1ULL << 4)
#define REIST_X86_64_HANDOFF_ELF64_PROBE     (1ULL << 5)
#define REIST_X86_64_HANDOFF_RING3            (1ULL << 6)
#define REIST_X86_64_HANDOFF_TIMER            (1ULL << 7)
#define REIST_X86_64_HANDOFF_LIFECYCLE        (1ULL << 8)
#define REIST_X86_64_HANDOFF_NO_DEVICE_AUTH   (1ULL << 9)
#define REIST_X86_64_HANDOFF_REQUIRED_FLAGS   0x3FFULL

#define REIST_X86_64_LIFECYCLE_GENERATIONS (1ULL << 0)
#define REIST_X86_64_LIFECYCLE_DEADLINES   (1ULL << 1)
#define REIST_X86_64_LIFECYCLE_SPAWN       (1ULL << 2)
#define REIST_X86_64_LIFECYCLE_WAIT        (1ULL << 3)
#define REIST_X86_64_LIFECYCLE_CAPS        0x0FULL

struct __attribute__((packed)) reist_x86_64_bootstrap_handoff_v1 {
    reist_u32 version;
    reist_u32 size;
    reist_u64 flags;
    reist_u64 higher_half_base;
    reist_u64 kernel_cr3;
    reist_u64 kernel_pml4;
    reist_u64 direct_map_base;
    reist_u64 managed_memory_limit;
    reist_u64 elf_probe_base;
    reist_u64 elf_probe_limit;
    reist_u64 kernel_text_start;
    reist_u64 kernel_text_end;
    reist_u64 lifecycle_capabilities;
    reist_u32 task_capacity;
    reist_u32 runqueue_capacity;
    reist_u32 deadline_capacity;
    reist_u32 syscall_abi_version;
    reist_u8 reserved[16];
};

_Static_assert(sizeof(struct reist_x86_64_bootstrap_handoff_v1) ==
                   REIST_X86_64_HANDOFF_SIZE,
               "x86_64 bootstrap handoff ABI size changed");

reist_u32 x86_64_c_core_entry(
    volatile struct reist_x86_64_bootstrap_handoff_v1 *handoff);

#endif
