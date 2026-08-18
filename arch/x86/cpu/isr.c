/**
 * @file arch/x86/cpu/isr.c
 * @brief C-Dispatch für CPU-Ausnahmen.
 *
 * Layer: Ring-0 x86 architecture and memory.
 * Contract: Binärlayouts, Adressgrenzen und Privilegien entsprechen der x86-Hardware-ABI.
 * Safety: Userfehler terminieren den Prozess; Kernfehler gehen in den Fatalpfad.
 */
#include "arch/x86/include/sys.h"
#include "arch/x86/include/interrupt.h"
#include "arch/x86/include/tss.h"
#include "include/kernel/panic.h"
#include "kernel/sched/scheduler.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"

// methods defined in the assembly file
extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();

const char* exception_messages[] =
{
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

_Static_assert(sizeof(exception_messages) / sizeof(exception_messages[0]) == 32,
               "exception message table must cover vectors 0 through 31");

// Define the type for exception handlers
typedef void (*ExceptionHandler)(Registers*);

// Declare an array to store handlers for each exception
ExceptionHandler exception_handlers[32];

static uint32_t exception_read_cr2(void) {
    uint32_t cr2;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

// void print_registers(Registers* r) {
//     // Print registers in a compact table to fit 80 characters width
//     printf("Registers:\n");
//     printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");
//     printf("|   gs   |   fs   |   es   |   ds   |   edi  |   esi  |   ebp  |   esp  |\n");
//     printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");
//     printf("| 0x%04X | 0x%04X | 0x%04X | 0x%04X | 0x%08X | 0x%08X | 0x%08X | 0x%08X |\n",
//            r->gs, r->fs, r->es, r->ds, r->edi, r->esi, r->ebp, r->esp);
//     printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");
//     printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");
//     printf("|   ebx  |   edx  |   ecx  |   eax  |  irq   |        |        |        |\n");
//     printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");
//     printf("| 0x%08X | 0x%08X | 0x%08X | 0x%08X |   %04d   |        |        |        |\n",
//            r->ebx, r->edx, r->ecx, r->eax, r->irq_number);
//     printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");
// }

// void guru_meditation_error(Registers* r) {
//     // Print an Amiga-style Guru Meditation message
//     printf("\n*** GURU MEDITATION ERROR ***\n\n");

//     // Print the error message and compact register table to fit 80 characters width
//     printf("An exception has occurred.\n\n");

//     printf("Registers:\n");
//     printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");
//     printf("|   gs   |   fs   |   es   |   ds   |   edi  |   esi  |   ebp  |   esp  |\n");
//     printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");
//     printf("| 0x%04X | 0x%04X | 0x%04X | 0x%04X | 0x%08X | 0x%08X | 0x%08X | 0x%08X |\n",
//            r->gs, r->fs, r->es, r->ds, r->edi, r->esi, r->ebp, r->esp);
//     printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");

//     printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");
//     printf("|   ebx  |   edx  |   ecx  |   eax  |  irq   |        |        |        |\n");
//     printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");
//     printf("| 0x%08X | 0x%08X | 0x%08X | 0x%08X |   %04d   |        |        |        |\n",
//            r->ebx, r->edx, r->ecx, r->eax, r->irq_number);
//     printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");

//     // Print the iconic Amiga error footer
//     printf("\nSYSTEM HALTED\n");
//     printf("Please restart or contact technical support.\n");
// }

// Generic exception handler
void generic_exception_handler(Registers* r) {
    // Check privilege level from CS register
    // CS & 0x3 gives Current Privilege Level (CPL)
    // 0 = Ring 0 (kernel), 3 = Ring 3 (user)
    uint16_t cpl = r->cs & 0x3;
    uint32_t cr2 = exception_read_cr2();

    if (cpl == 0) {
        // Kernel exception - unrecoverable
        char panic_msg[128];
        snprintf(panic_msg, sizeof(panic_msg), 
                 "Kernel exception: %s (IRQ %d) at EIP=0x%08X",
                 exception_messages[r->irq_number], 
                 r->irq_number, 
                 r->eip);
        panic_with_exception(panic_msg, r, cr2);
    } else {
        // User mode exception - terminate process
        printf("\n*** USER PROCESS EXCEPTION ***\n");
        printf("Exception: %s (IRQ %d)\n", 
               exception_messages[r->irq_number], 
               r->irq_number);
        printf("EIP: 0x%08X, CS: 0x%04X (Ring %d)\n", r->eip, r->cs, cpl);
        panic_dump_exception_context(r, cr2);
        printf("Process terminated.\n\n");
        
        task_exit_status(128 + (int)r->irq_number);
    }
}

// Divide by zero handler (specific override)
void divide_by_zero_handler(Registers* r) {
    uint16_t cpl = r->cs & 0x3;
    uint32_t cr2 = exception_read_cr2();
    
    if (cpl == 0) {
        // Kernel divide by zero - unrecoverable
        char panic_msg[128];
        snprintf(panic_msg, sizeof(panic_msg),
                 "Kernel divide by zero at EIP=0x%08X", r->eip);
        panic_with_exception(panic_msg, r, cr2);
    } else {
        // User mode divide by zero
        printf("\n*** USER PROCESS ERROR ***\n");
        printf("Divide by zero exception at EIP=0x%08X\n", r->eip);
        panic_dump_exception_context(r, cr2);
        printf("Process terminated.\n\n");
        
        task_exit_status(128);
    }
}

void page_fault_handler(Registers* r) {
    uint32_t faulting_address;
    uint32_t error_code = r->error_code;
    
    // Read the faulting address from CR2
    faulting_address = exception_read_cr2();
    
    // The saved CS describes the faulting context; the current CS is Ring 0.
    uint16_t cpl = r->cs & 0x3;
    
    if (cpl == 0) {
        // Kernel page fault - unrecoverable
        char panic_msg[128];
        snprintf(panic_msg, sizeof(panic_msg),
                 "Kernel page fault at address 0x%08X (error code: 0x%X)",
                 faulting_address, error_code);
        panic_with_exception(panic_msg, r, faulting_address);
    } else {
        // User mode page fault
        printf("\n*** USER PROCESS PAGE FAULT ***\n");
        printf("Faulting address: 0x%08X\n", faulting_address);
        printf("Error code: 0x%X ", error_code);
        
        // Decode error code
        printf("(");
        if (error_code & 0x1) printf("protection violation"); else printf("page not present");
        if (error_code & 0x2) printf(", write"); else printf(", read");
        if (error_code & 0x4) printf(", user mode"); else printf(", kernel mode");
        printf(")\n");
        panic_dump_exception_context(r, faulting_address);
        
        printf("Process terminated.\n\n");
        
        task_exit_status(142);
    }
}

// Set up the exception handlers
void setup_exceptions() {
    // Set all entries to the generic exception handler
    for (int i = 1; i < 32; i++) {
        exception_handlers[i] = generic_exception_handler;
    }

    // Override specific handlers
    exception_handlers[0] = divide_by_zero_handler;  // Divide by zero
    exception_handlers[14] = page_fault_handler;
}

void exception_dispatcher(Registers* state) {
    if (state == NULL || state->irq_number >= 32 ||
        exception_handlers[state->irq_number] == NULL) {
        panic("Invalid CPU exception frame");
    }
    exception_handlers[state->irq_number](state);
}

void rtl8139_handler(Registers* r) {
    printf("+++RTL8139+++ Interrupt\n");
    //rtl8139_interrupt_handler();
}

void isr_install() {
    // set the IDT entries defined in the assembly file
    set_idt_entry(0, (uint32_t)isr0);
    set_idt_entry(1, (uint32_t)isr1);
    set_idt_entry(2, (uint32_t)isr2);
    set_idt_entry(3, (uint32_t)isr3);
    set_idt_entry(4, (uint32_t)isr4);
    set_idt_entry(5, (uint32_t)isr5);
    set_idt_entry(6, (uint32_t)isr6);
    set_idt_entry(7, (uint32_t)isr7);
    set_idt_entry(8, (uint32_t)isr8);
    set_idt_entry(9, (uint32_t)isr9);
    set_idt_entry(10, (uint32_t)isr10);
    set_idt_entry(11, (uint32_t)isr11);
    set_idt_entry(12, (uint32_t)isr12);
    set_idt_entry(13, (uint32_t)isr13);
    set_idt_entry(14, (uint32_t)isr14);
    set_idt_entry(15, (uint32_t)isr15);
    set_idt_entry(16, (uint32_t)isr16);
    set_idt_entry(17, (uint32_t)isr17);
    set_idt_entry(18, (uint32_t)isr18);
    set_idt_entry(19, (uint32_t)isr19);
    set_idt_entry(20, (uint32_t)isr20);
    set_idt_entry(21, (uint32_t)isr21);
    set_idt_entry(22, (uint32_t)isr22);
    set_idt_entry(23, (uint32_t)isr23);
    set_idt_entry(24, (uint32_t)isr24);
    set_idt_entry(25, (uint32_t)isr25);
    set_idt_entry(26, (uint32_t)isr26);
    set_idt_entry(27, (uint32_t)isr27);
    set_idt_entry(28, (uint32_t)isr28);
    set_idt_entry(29, (uint32_t)isr29);
    set_idt_entry(30, (uint32_t)isr30);
    set_idt_entry(31, (uint32_t)isr31);

    /* A normal interrupt gate would reuse a potentially destroyed stack and
     * can turn #DF into an invisible triple fault. */
    set_idt_task_gate(8, DOUBLE_FAULT_TSS_SELECTOR);

    // Set up the exception handlers

    setup_exceptions();
}
