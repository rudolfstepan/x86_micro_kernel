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

#pragma pack(push, 1)
typedef struct
{
    unsigned int int_no;    /* our 'push byte #' and ecodes do this */
} Registers2;
#pragma pack(pop)

// Prototype for the external assembly function
extern void trigger_interrupt(void* regs);

//gdt
extern void gdt_install();

//idt
extern void idt_install();
extern void idt_set_gate(unsigned char num, unsigned long base, unsigned short sel, unsigned char flags);

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