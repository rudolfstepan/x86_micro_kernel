#include "arch/x86/sys.h"
#include "arch/x86/include/tss.h"


// Global Descriptor Table
// The GDT is a structure used by the x86 architecture to define memory segments and their access rights.
// The GDT is loaded into the GDTR register using the LGDT instruction.
//
// GDT Layout (6 entries):
// 0: NULL descriptor (required by x86)
// 1: Kernel Code Segment (Ring 0, selector 0x08)
// 2: Kernel Data Segment (Ring 0, selector 0x10)
// 3: User Code Segment   (Ring 3, selector 0x18)
// 4: User Data Segment   (Ring 3, selector 0x20)
// 5: TSS (Task State Segment, selector 0x28)


struct gdt_entry {
    unsigned short limit_low;
    unsigned short base_low;
    unsigned char base_middle;
    unsigned char access;
    unsigned char granularity;
    unsigned char base_high;
} __attribute__((packed));

struct gdt_ptr {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));


struct gdt_entry gdt[6];  // Expanded from 3 to 6 entries
struct gdt_ptr gp;

extern void gdt_flush();

// Assembly function to load TSS
extern void tss_flush(uint16_t selector);

void gdt_set_gate(int num, unsigned long base, unsigned long limit, unsigned char access, unsigned char gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;

    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F);

    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access = access;
}

void gdt_install() {
    gp.limit = (sizeof(struct gdt_entry) * 6) - 1;  // 6 entries now
    gp.base = (unsigned)&gdt;

    // Entry 0: NULL descriptor (required by x86 architecture)
    gdt_set_gate(0, 0, 0, 0, 0);
    
    // Entry 1: Kernel Code Segment (Ring 0)
    // Base: 0x00000000, Limit: 0xFFFFFFFF (4GB)
    // Access: 0x9A = Present, Ring 0, Code, Executable, Readable
    // Granularity: 0xCF = 4KB pages, 32-bit protected mode
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    
    // Entry 2: Kernel Data Segment (Ring 0)
    // Base: 0x00000000, Limit: 0xFFFFFFFF (4GB)
    // Access: 0x92 = Present, Ring 0, Data, Writable
    // Granularity: 0xCF = 4KB pages, 32-bit protected mode
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    
    // Entry 3: User Code Segment (Ring 3)
    // Base: 0x00000000, Limit: 0xFFFFFFFF (4GB)
    // Access: 0xFA = Present, Ring 3, Code, Executable, Readable
    // Granularity: 0xCF = 4KB pages, 32-bit protected mode
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    
    // Entry 4: User Data Segment (Ring 3)
    // Base: 0x00000000, Limit: 0xFFFFFFFF (4GB)
    // Access: 0xF2 = Present, Ring 3, Data, Writable
    // Granularity: 0xCF = 4KB pages, 32-bit protected mode
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);
    
    // Entry 5: Task State Segment (TSS)
    // Base: Address of TSS structure
    // Limit: sizeof(TSS) - 1
    // Access: 0x89 = Present, Ring 0, TSS (32-bit available)
    // Granularity: 0x00 = Byte granularity (not 4KB pages)
    uint32_t tss_base = tss_get_base();
    uint32_t tss_limit = tss_get_limit();
    gdt_set_gate(5, tss_base, tss_limit, 0x89, 0x00);

    // Flush out the old GDT and install the new changes!
    // method is in gdt.asm
    gdt_flush();
    
    // Load TSS into Task Register (TR)
    // Selector 0x28 = index 5, RPL 0 (Ring 0)
    // 0x28 = (5 << 3) | 0
    tss_flush(0x28);
}
