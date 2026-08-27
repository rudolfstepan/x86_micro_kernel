/**
 * @file arch/x86/cpu/irq.c
 * @brief PIC- und Hardware-IRQ-Dispatch.
 *
 * Layer: Ring-0 x86 architecture and memory.
 * Contract: Binärlayouts, Adressgrenzen und Privilegien entsprechen der x86-Hardware-ABI.
 * Safety: EOI-Reihenfolge und Handlergrenzen verhindern verlorene oder endlose IRQs.
 */
#include "drivers/char/io.h"
#include "arch/x86/include/sys.h"
#include "lib/libc/stdio.h"
#include "kernel/sched/scheduler.h"
#include "arch/x86/include/interrupt.h"
#include "arch/x86/include/cpu_local.h"
#include "arch/x86/include/smp.h"
#include "arch/x86/mm/paging.h"
#include "include/kernel/panic.h"

extern char _kernel_start;
extern char _kernel_text_end;

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
extern void smp_scheduler_release_interrupt(void);
extern void apic_spurious_interrupt();
extern void tlb_shootdown_interrupt();

//extern void syscall_handler_asm();

// Array of IRQ handler routines for custom handlers
#define IRQ_ROUTINE_COUNT 16
#define IRQ_HANDLERS_PER_LINE 4
static void* irq_routines[IRQ_ROUTINE_COUNT][IRQ_HANDLERS_PER_LINE] = {{0}};
static volatile uint32_t irq_affinity[IRQ_ROUTINE_COUNT];
static volatile uint32_t irq_affinity_violations;
#define IRQ_BSP_AFFINITY_MASK 1U
#define current_irq_context_depth \
    (x86_cpu_local_current()->irq_context_depth)
void irq_context_enter(void) {
    KASSERT(x86_cpu_local_current() != NULL);
    KASSERT(current_irq_context_depth < UINT32_MAX);
    ++current_irq_context_depth;
}

void irq_context_note_vector(uint32_t vector) {
    x86_cpu_local_t *local = x86_cpu_local_current();
    KASSERT(local != NULL);
    KASSERT(local->irq_context_depth == 0U);
    local->irq_context_vector = vector;
}

void irq_context_exit(void) {
    KASSERT(x86_cpu_local_current() != NULL);
    KASSERT(current_irq_context_depth != 0);
    --current_irq_context_depth;
    if (current_irq_context_depth == 0U)
        x86_cpu_local_current()->irq_context_vector = 0U;
}

int irq_in_context(void) {
    if (x86_cpu_local_current() == NULL) return 1;
    return current_irq_context_depth != 0;
}

uint32_t irq_context_vector(void) {
    x86_cpu_local_t *local = x86_cpu_local_current();
    return local != NULL ? local->irq_context_vector : UINT32_MAX;
}

// Function to install a custom IRQ handler
int register_interrupt_handler(int irq, void* r) {
    uintptr_t handler = (uintptr_t)r;
    if (irq < 0 || irq >= IRQ_ROUTINE_COUNT ||
        handler < (uintptr_t)&_kernel_start ||
        handler >= (uintptr_t)&_kernel_text_end) {
        return -1;
    }
    uint32_t flags = irq_save();
    for (int slot = 0; slot < IRQ_HANDLERS_PER_LINE; ++slot) {
        if (irq_routines[irq][slot] == r) {
            irq_restore(flags);
            return 0;
        }
        if (irq_routines[irq][slot] == NULL) {
            irq_routines[irq][slot] = r;
            irq_restore(flags);
            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}

int unregister_interrupt_handler(int irq, void *r) {
    if (irq < 0 || irq >= IRQ_ROUTINE_COUNT || r == NULL) return -1;
    uint32_t flags = irq_save();
    for (int slot = 0; slot < IRQ_HANDLERS_PER_LINE; ++slot) {
        if (irq_routines[irq][slot] == r) {
            irq_routines[irq][slot] = NULL;
            irq_restore(flags);
            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}

// Function to uninstall an IRQ handler
void irq_uninstall_handler(int irq) {
    if (irq < 0 || irq >= IRQ_ROUTINE_COUNT) {
        return;
    }
    uint32_t flags = irq_save();
    for (int slot = 0; slot < IRQ_HANDLERS_PER_LINE; ++slot) {
        irq_routines[irq][slot] = NULL;
    }
    irq_restore(flags);
}

static bool irq_pic_update_line(uint8_t irq, bool masked) {
    if (irq >= IRQ_ROUTINE_COUNT) return false;
    uint16_t port = irq < 8U ? 0x21U : 0xA1U;
    uint8_t bit = irq < 8U ? irq : (uint8_t)(irq - 8U);
    uint32_t flags = irq_save();
    uint8_t value = inb(port);
    if (masked) value |= (uint8_t)(1U << bit);
    else value &= (uint8_t)~(1U << bit);
    outb(port, value);
    bool updated = (inb(port) & (uint8_t)(1U << bit)) ==
        (masked ? (uint8_t)(1U << bit) : 0U);
    irq_restore(flags);
    return updated;
}

bool irq_pic_mask_line(uint8_t irq) {
    return irq_pic_update_line(irq, true);
}

bool irq_pic_unmask_line(uint8_t irq) {
    return irq_pic_update_line(irq, false);
}

int irq_set_affinity(uint8_t irq, uint32_t cpu_mask) {
    if (irq >= IRQ_ROUTINE_COUNT || cpu_mask != IRQ_BSP_AFFINITY_MASK)
        return -95;
    uint32_t flags = irq_save();
    irq_affinity[irq] = cpu_mask;
    __sync_synchronize();
    irq_restore(flags);
    return 0;
}

uint32_t irq_affinity_mask(uint8_t irq) {
    if (irq >= IRQ_ROUTINE_COUNT) return 0U;
    __sync_synchronize();
    return irq_affinity[irq];
}

bool irq_affinity_bsp_only_ready(void) {
    for (uint8_t irq = 0U; irq < IRQ_ROUTINE_COUNT; ++irq) {
        if (irq_affinity_mask(irq) != IRQ_BSP_AFFINITY_MASK) return false;
    }
    return true;
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
    for (uint8_t irq = 0U; irq < IRQ_ROUTINE_COUNT; ++irq)
        irq_affinity[irq] = IRQ_BSP_AFFINITY_MASK;
    irq_affinity_violations = 0U;
    __sync_synchronize();
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

    // DPL=3 permits INT 0x80 from user mode; the handler still executes in Ring 0.
    set_idt_entry_flags(0x80, (uint32_t)syscall_handler_asm, 0xEE);
    set_idt_entry(0xFF, (uint32_t)apic_spurious_interrupt);
    set_idt_entry(X86_TLB_SHOOTDOWN_VECTOR,
                  (uint32_t)tlb_shootdown_interrupt);
    set_idt_entry(X86_SMP_SCHEDULER_RELEASE_VECTOR,
                  (uint32_t)smp_scheduler_release_interrupt);
}

// General IRQ handler that checks for custom routines
void irq_handler(Registers* regs) {
    if (regs == NULL || regs->irq_number < 32 || regs->irq_number >= 48) {
        return;
    }

    irq_context_note_vector(regs->irq_number);
    irq_context_enter();
    uint32_t irq = regs->irq_number - 32;
    uint32_t cpu = x86_cpu_current_index();
    bool affinity_allowed = cpu < X86_CPU_LOCAL_MAX &&
        (irq_affinity[irq] & (1U << cpu)) != 0U;
    if (!affinity_allowed) {
        ++irq_affinity_violations;
        (void)irq_pic_mask_line((uint8_t)irq);
    } else {
        // Legacy PCI lines may be shared. Every registered handler must
        // inspect its device status and ignore unrelated interrupts.
        for (int slot = 0; slot < IRQ_HANDLERS_PER_LINE; ++slot) {
            void (*handler)(Registers* r) =
                (void (*)(Registers*))(irq_routines[irq][slot]);
            if (handler != NULL) handler(regs);
        }
    }

    // Send End of Interrupt (EOI) to the PICs if necessary
    if (regs->irq_number >= 40) {
        outb(0xA0, 0x20); // Send EOI to slave PIC
    }
    outb(0x20, 0x20);     // Send EOI to master PIC

    /* A context switch must happen only after the PIC has acknowledged IRQ0;
     * otherwise the parked interrupt frame leaves the timer in-service. */
    irq_context_exit();
    if (affinity_allowed && irq == 0) scheduler_pit_interrupt_handler();
}
