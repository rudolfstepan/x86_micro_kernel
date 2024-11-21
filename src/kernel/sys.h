#ifndef SYS_H
#define SYS_H

#include <stdint.h>

// Structure for the registers
#pragma pack(push, 1)
typedef struct
{
    uint32_t gs, fs, es, ds;  // Segment registers
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  // General-purpose registers
    uint32_t irq_number;      // Pushed by ISR
    uint32_t error_code;      // Pushed by ISR or CPU
    uint32_t eip, cs, eflags, useresp, ss; // Automatically pushed by CPU
} Registers;
#pragma pack(pop)

//gdt
extern void gdt_install();

//idt
extern void idt_install();
extern void set_idt_entry(int vector, uint32_t handler);

//isr
extern void isr_install();

//irq
extern void irq_install();
extern void irq_handler(Registers* r);
extern void irq_install_handler(int irq, void* r);
void irq_uninstall_handler(int irq);

extern volatile uint64_t cpu_frequency; // Global CPU frequency


#endif