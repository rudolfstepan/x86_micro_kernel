#ifndef SYS_H
#define SYS_H

#include <stdint.h>

// Structure for the registers
#pragma pack(push, 1)
typedef struct
{
    unsigned int gs, fs, es, ds;      /* pushed the segs last */
    unsigned int edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pushed by 'pusha' */
    unsigned int int_no, err_code;    /* our 'push byte #' and ecodes do this */
    unsigned int eip, cs, eflags, useresp, ss;   /* pushed by the processor automatically */ 
} Registers;
#pragma pack(pop)

//gdt
extern void gdt_install();

//idt
extern void idt_install();
extern void set_idt_entry(int vector, uint32_t handler, uint16_t selector, uint8_t flags);

//isr
extern void isr_install();

//irq
extern void irq_install();
extern void irq_handler(Registers* r);
extern void irq_install_handler(int irq, void* r);
void irq_uninstall_handler(int irq);

//pit
//extern void timer_install();


#endif