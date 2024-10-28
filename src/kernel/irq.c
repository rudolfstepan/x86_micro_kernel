#include "drivers/io/io.h"

#include "sys.h"


extern void irq0();
extern void irq1();
extern void irq2();
extern void irq3();
extern void irq4();
extern void irq5();
extern void irq6();
extern void irq7();
extern void irq8();
extern void irq9();
extern void irq10();
extern void irq11();
extern void irq12();
extern void irq13();
extern void irq14();
extern void irq15();

void* irq_routines[17] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // IRQ0 - IRQ15
};

void irq_install_handler(int irq, void (*handler)(struct regs* r)) {
    irq_routines[irq] = handler;
}

void irq_uninstall_handler(int irq) {
    irq_routines[irq] = 0;
}

void irq_remap(void) {
    outb(0x20, 0x11); // initialise
    outb(0xA0, 0x11);
    outb(0x21, 0x20); //address - icw2
    outb(0xA1, 0x28);
    outb(0x21, 0x04); // icw3
    outb(0xA1, 0x02);
    outb(0x21, 0x01); // icw4
    outb(0xA1, 0x01);
    outb(0x21, 0x0); // clear out command register
    outb(0xA1, 0x0);
}

void irq_install() {
    irq_remap();

    // idt_set_gate(32, (unsigned)irq0, 0x08, 0x8E);
    // idt_set_gate(33, (unsigned)irq1, 0x08, 0x8E);
    // idt_set_gate(34, (unsigned)irq2, 0x08, 0x8E);
    // idt_set_gate(35, (unsigned)irq3, 0x08, 0x8E);
    // idt_set_gate(36, (unsigned)irq4, 0x08, 0x8E);
    // idt_set_gate(37, (unsigned)irq5, 0x08, 0x8E);
    // idt_set_gate(38, (unsigned)irq6, 0x08, 0x8E);
    // idt_set_gate(39, (unsigned)irq7, 0x08, 0x8E);
    // idt_set_gate(40, (unsigned)irq8, 0x08, 0x8E);
    // idt_set_gate(41, (unsigned)irq9, 0x08, 0x8E);
    // idt_set_gate(42, (unsigned)irq10, 0x08, 0x8E);
    // idt_set_gate(43, (unsigned)irq11, 0x08, 0x8E);
    // idt_set_gate(44, (unsigned)irq12, 0x08, 0x8E);
    // idt_set_gate(45, (unsigned)irq13, 0x08, 0x8E);
    // idt_set_gate(46, (unsigned)irq14, 0x08, 0x8E);
    // idt_set_gate(47, (unsigned)irq15, 0x08, 0x8E);
    // idt_set_gate(128, (unsigned)irq_syscall, 0x08, 0x8E);

    idt_set_gate((int)0x20, (unsigned)irq0, 0x08, 0x8E);
    idt_set_gate((int)0x21, (unsigned)irq1, 0x08, 0x8E);
    idt_set_gate((int)0x22, (unsigned)irq2, 0x08, 0x8E);
    idt_set_gate((int)0x23, (unsigned)irq3, 0x08, 0x8E);
    idt_set_gate((int)0x24, (unsigned)irq4, 0x08, 0x8E);
    idt_set_gate((int)0x25, (unsigned)irq5, 0x08, 0x8E);
    idt_set_gate((int)0x26, (unsigned)irq6, 0x08, 0x8E);
    idt_set_gate((int)0x27, (unsigned)irq7, 0x08, 0x8E);
    idt_set_gate((int)0x28, (unsigned)irq8, 0x08, 0x8E);
    idt_set_gate((int)0x29, (unsigned)irq9, 0x08, 0x8E);
    idt_set_gate((int)0x2A, (unsigned)irq10, 0x08, 0x8E);
    idt_set_gate((int)0x2B, (unsigned)irq11, 0x08, 0x8E);
    idt_set_gate((int)0x2C, (unsigned)irq12, 0x08, 0x8E);
    idt_set_gate((int)0x2D, (unsigned)irq13, 0x08, 0x8E);
    idt_set_gate((int)0x2E, (unsigned)irq14, 0x08, 0x8E);
    idt_set_gate((int)0x2F, (unsigned)irq15, 0x08, 0x8E);
    idt_set_gate((int)0x80, (unsigned)irq15, 0x08, 0x8E);

}

void irq_handler(struct regs* r) {
    void (*handler)(struct regs* r);
    handler = irq_routines[r->int_no - 32];

    if (handler) {
        handler(r);
    }

    if (r->int_no >= 40) {
        // EOI for slave
        outb(0xA0, 0x20);
    }
    // EOI for master
    outb(0x20, 0x20);
}
