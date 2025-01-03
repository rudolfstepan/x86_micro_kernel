#include "kernel/sys.h"
#include "toolchain/stdlib.h"
#include "toolchain/string.h"


// Structure for an IDT entry
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
}  __attribute__((packed));

struct idt_ptr {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));
struct idt_ptr idtp;

#define NUM_IDT_ENTRIES 256

// Declare the IDT
struct idt_entry idt[256];

extern void idt_load(); // defined in boot.asm

// Function to set an IDT entry
void set_idt_entry(int vector, uint32_t handler) {
    idt[vector].offset_low = handler & 0xFFFF;
    idt[vector].selector = 0x08; // Code segment
    idt[vector].zero = 0;
    idt[vector].type_attr = 0x8E; // Interrupt gate
    idt[vector].offset_high = (handler >> 16) & 0xFFFF;
}

void idt_install() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (unsigned)&idt;
    memset((unsigned char*)&idt, 0, sizeof(struct idt_entry) * 256);

    //set gate entries
    idt_load();
}
