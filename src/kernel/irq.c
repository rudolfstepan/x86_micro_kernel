#include "drivers/io/io.h"
#include "sys.h"
#include "toolchain/stdio.h"

// External IRQ handlers defined in assembly or elsewhere
extern void irq0();
extern void irq1();
extern void irq2();
extern void irq3();
extern void irq4();
extern void irq5();
extern void irq6(); // IRQ6 for the FDC
extern void irq7();
extern void irq8();
extern void irq9();
extern void irq10();
extern void irq11();
extern void irq12();
extern void irq13();
extern void irq14();
extern void irq15();

// Array of IRQ handler routines for custom handlers
void* irq_routines[16] = { 0 };

// Function to install a custom IRQ handler
void irq_install_handler(int irq, void* r) {
    irq_routines[irq] = r;
}

// Function to uninstall an IRQ handler
void irq_uninstall_handler(int irq) {
    irq_routines[irq] = 0;
}

// Remaps IRQs 0-15 to interrupt vectors 0x20-0x2F
void irq_remap(void) {
    outb(0x20, 0x11); // Init command for PIC1
    outb(0xA0, 0x11); // Init command for PIC2
    outb(0x21, 0x20); // PIC1 vector offset to 0x20
    outb(0xA1, 0x28); // PIC2 vector offset to 0x28
    outb(0x21, 0x04); // Tell PIC1 about PIC2 at IRQ2
    outb(0xA1, 0x02); // Tell PIC2 its cascade identity
    outb(0x21, 0x01); // 8086/88 (MCS-80/85) mode for PIC1
    outb(0xA1, 0x01); // 8086/88 (MCS-80/85) mode for PIC2
    outb(0x21, 0x0);  // Unmask all interrupts on PIC1
    outb(0xA1, 0x0);  // Unmask all interrupts on PIC2
}

// Installs all IRQs to the IDT
void irq_install() {
    irq_remap();

    idt_set_gate(0x20, (unsigned)irq0, 0x08, 0x8E);
    idt_set_gate(0x21, (unsigned)irq1, 0x08, 0x8E);
    idt_set_gate(0x22, (unsigned)irq2, 0x08, 0x8E);
    idt_set_gate(0x23, (unsigned)irq3, 0x08, 0x8E);
    idt_set_gate(0x24, (unsigned)irq4, 0x08, 0x8E);
    idt_set_gate(0x25, (unsigned)irq5, 0x08, 0x8E);
    idt_set_gate(0x26, (unsigned)irq6, 0x08, 0x8E); // FDC IRQ
    idt_set_gate(0x27, (unsigned)irq7, 0x08, 0x8E);
    idt_set_gate(0x28, (unsigned)irq8, 0x08, 0x8E);
    idt_set_gate(0x29, (unsigned)irq9, 0x08, 0x8E);
    idt_set_gate(0x2A, (unsigned)irq10, 0x08, 0x8E);
    idt_set_gate(0x2B, (unsigned)irq11, 0x08, 0x8E);
    idt_set_gate(0x2C, (unsigned)irq12, 0x08, 0x8E);
    idt_set_gate(0x2D, (unsigned)irq13, 0x08, 0x8E);
    idt_set_gate(0x2E, (unsigned)irq14, 0x08, 0x8E);
    idt_set_gate(0x2F, (unsigned)irq15, 0x08, 0x8E);
}

// General IRQ handler that checks for custom routines
void irq_handler(Registers* regs) {
    // Check if there is a custom handler for this IRQ
    if (irq_routines[regs->int_no - 32]) {
        void (*handler)(Registers* r) = irq_routines[regs->int_no - 32];

        if(handler != NULL) {
            handler(regs);

            if(regs->int_no - 32 > 1) {
                printf("IRQ %d\n", regs->int_no - 32);
            }
        }
    }

    // Send End of Interrupt (EOI) to the PICs if necessary
    if (regs->int_no >= 40) {
        outb(0xA0, 0x20); // Send EOI to slave PIC
    }
    outb(0x20, 0x20);     // Send EOI to master PIC
}
