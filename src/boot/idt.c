#include "kernel/sys.h"
#include "kernel/system.h"

#include "toolchain/stdlib.h"
#include "toolchain/strings.h"

struct idt_entry {
    unsigned short base_lo;
    unsigned short sel;
    unsigned char always0;
    unsigned char flags;
    unsigned short base_hi;
} __attribute__((packed));

struct idt_ptr {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idt_ptr idtp;


extern void idt_load();


void idt_set_gate(unsigned char num, unsigned long base, unsigned short sel, unsigned char flags) {
    idt[num].always0 = 0;
    idt[num].flags = flags;
    idt[num].sel = sel;
    idt[num].base_lo = (base & 0xFFFF);
    idt[num].base_hi = ((base >> 16) & 0xFFFF);
}

void idt_install() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (unsigned)&idt;
    memset((unsigned char*)&idt, 0, sizeof(struct idt_entry) * 256);

    //set gate entries
    idt_load();

    //printf("* idt install\n");
}
