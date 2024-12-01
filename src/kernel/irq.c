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

//extern void syscall_handler_asm();

// Array of IRQ handler routines for custom handlers
void* irq_routines[17] = { 0 };

// Function to install a custom IRQ handler
void register_interrupt_handler(int irq, void* r) {
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

extern void syscall_handler_asm();

// Installs all IRQs to the IDT
void irq_install() {
    irq_remap();

    set_idt_entry(0x20, (uint32_t)irq0);  // Timer Interrupt (PIT/APIC Timer)
    set_idt_entry(0x21, (uint32_t)irq1);  // Keyboard Interrupt
    set_idt_entry(0x22, (uint32_t)irq2);  // Cascade (used for chained PICs)
    set_idt_entry(0x23, (uint32_t)irq3);  // COM2/COM4 (Serial Port)
    set_idt_entry(0x24, (uint32_t)irq4);  // COM1/COM3 (Serial Port)
    set_idt_entry(0x25, (uint32_t)irq5);  // LPT2 or Sound Card
    set_idt_entry(0x26, (uint32_t)irq6);  // Floppy Disk Controller (FDC)
    set_idt_entry(0x27, (uint32_t)irq7);  // LPT1 (Parallel Port) or Spurious IRQ
    set_idt_entry(0x28, (uint32_t)irq8);  // Real-Time Clock (RTC)
    set_idt_entry(0x29, (uint32_t)irq9);  // ACPI or Free for General Use
    set_idt_entry(0x2A, (uint32_t)irq10); // Free for General Use (e.g., Network Card)
    set_idt_entry(0x2B, (uint32_t)irq11); // Free for General Use (e.g., SCSI or USB)
    set_idt_entry(0x2C, (uint32_t)irq12); // PS/2 Mouse
    set_idt_entry(0x2D, (uint32_t)irq13); // Floating-Point Unit (FPU)/Coprocessor
    set_idt_entry(0x2E, (uint32_t)irq14); // Primary ATA Hard Disk
    set_idt_entry(0x2F, (uint32_t)irq15); // Secondary ATA Hard Disk

    set_idt_entry(0x80, (uint32_t)syscall_handler_asm); // System call gate
}

// General IRQ handler that checks for custom routines
void irq_handler(Registers* regs) {
    // Check if there is a custom handler for this IRQ
    if (irq_routines[regs->irq_number - 32]) {
        void (*handler)(Registers* r) = (void (*)(Registers*))(irq_routines[regs->irq_number - 32]);

            // if(regs->irq_number - 32 > 0) {
            //     printf("registered RQ %d\n", regs->irq_number - 32);
            // }

        if(handler != NULL) {
            handler(regs);
        }
    }

    // Send End of Interrupt (EOI) to the PICs if necessary
    if (regs->irq_number >= 40) {
        outb(0xA0, 0x20); // Send EOI to slave PIC
    }
    outb(0x20, 0x20);     // Send EOI to master PIC
}
