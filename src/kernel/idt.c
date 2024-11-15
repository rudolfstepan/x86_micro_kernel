#include "kernel/sys.h"
#include "toolchain/stdlib.h"
#include "toolchain/strings.h"


// Structure for an IDT entry
struct idt_entry {
    uint16_t base_low;
    uint16_t selector;     // Kernel code segment selector in GDT
    uint8_t zero;          // Always zero
    uint8_t flags;         // Type and attributes
    uint16_t base_high;
}  __attribute__((packed));

// Declare the IDT
struct idt_entry idt[256];

struct idt_ptr {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));
struct idt_ptr idtp;

extern void idt_load(); // defined in boot.asm

// Function to set an IDT entry
void set_idt_entry(int vector, uint32_t handler, uint16_t selector, uint8_t flags) {
    idt[vector].base_low = handler & 0xFFFF;
    idt[vector].selector = selector;
    idt[vector].zero = 0;
    idt[vector].flags = flags;
    idt[vector].base_high = (handler >> 16) & 0xFFFF;
}

void idt_install() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (unsigned)&idt;
    memset((unsigned char*)&idt, 0, sizeof(struct idt_entry) * 256);

    //set gate entries
    idt_load();
}
