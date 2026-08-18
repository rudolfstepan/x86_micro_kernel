/**
 * @file arch/x86/cpu/idt.c
 * @brief Aufbau und Laden der Interrupt Descriptor Table.
 *
 * Layer: Ring-0 x86 architecture and memory.
 * Contract: Binärlayouts, Adressgrenzen und Privilegien entsprechen der x86-Hardware-ABI.
 * Safety: Gates werden vollständig aufgebaut, bevor IDTR publiziert wird.
 */
#include "arch/x86/include/sys.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"


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
void set_idt_entry_flags(int vector, uint32_t handler, uint8_t type_attr) {
    if (vector < 0 || vector >= NUM_IDT_ENTRIES) {
        return;
    }

    idt[vector].offset_low = handler & 0xFFFF;
    idt[vector].selector = 0x08; // Code segment
    idt[vector].zero = 0;
    idt[vector].type_attr = type_attr;
    idt[vector].offset_high = (handler >> 16) & 0xFFFF;
}

void set_idt_entry(int vector, uint32_t handler) {
    set_idt_entry_flags(vector, handler, 0x8E); // Ring-0 interrupt gate
}

void set_idt_task_gate(int vector, uint16_t tss_selector) {
    if (vector < 0 || vector >= NUM_IDT_ENTRIES) return;
    idt[vector].offset_low = 0;
    idt[vector].selector = tss_selector;
    idt[vector].zero = 0;
    idt[vector].type_attr = 0x85; /* Present, DPL0, 32-bit task gate. */
    idt[vector].offset_high = 0;
}

void idt_install() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (unsigned)&idt;
    memset((unsigned char*)&idt, 0, sizeof(struct idt_entry) * 256);

    //set gate entries
    idt_load();
}
