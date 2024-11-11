#ifndef SYS_H
#define SYS_H

#include <stdint.h>

struct regs
{
    unsigned int gs, fs, es, ds;      /* pushed the segs last */
    unsigned int edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pushed by 'pusha' */
    unsigned int int_no, err_code;    /* our 'push byte #' and ecodes do this */
    unsigned int eip, cs, eflags, useresp, ss;   /* pushed by the processor automatically */ 
};



// Define a struct to hold register values and the interrupt number
typedef struct {
    uint16_t ax;
    uint16_t bx;
    uint16_t cx;
    uint16_t dx;
    uint16_t si;
    uint16_t di;
    uint16_t es;
    uint16_t ds;
    uint8_t interruptNumber; // The interrupt number to be called
} Registers;
// Prototype for the external assembly function
extern void trigger_interrupt(Registers *regs);


//gdt
extern void gdt_install();

//idt
extern void idt_install();
extern void idt_set_gate(unsigned char num, unsigned long base, unsigned short sel, unsigned char flags);

//isr
extern void isr_install();

//irq
extern void irq_install();
extern void irq_handler(void* r);
extern void irq_install_handler(int irq, void* r);
void irq_uninstall_handler(int irq);

//pit
//extern void timer_install();


#endif