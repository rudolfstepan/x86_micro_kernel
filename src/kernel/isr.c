#include "kernel/sys.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"

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


char* exception_messages[] =
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
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved"
};

void fault_handler(Registers* r) {
    if (r->irq_number < 32) {
        // Display the exception message
        printf("System Exception: %s\n\n", exception_messages[(int)r->irq_number]);

        // Print registers in a compact table to fit 80 characters width
        // printf("Registers:\n");
        // printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");
        // printf("|   gs   |   fs   |   es   |   ds   |   edi  |   esi  |   ebp  |   esp  |\n");
        // printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");
        // printf("| 0x%04X | 0x%04X | 0x%04X | 0x%04X | 0x%08X | 0x%08X | 0x%08X | 0x%08X |\n",
        //        r->gs, r->fs, r->es, r->ds, r->edi, r->esi, r->ebp, r->esp);
        // printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");

        // printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");
        // printf("|   ebx  |   edx  |   ecx  |   eax  |  irq   |        |        |        |\n");
        // printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");
        // printf("| 0x%08X | 0x%08X | 0x%08X | 0x%08X |   %04d   |        |        |        |\n",
        //        r->ebx, r->edx, r->ecx, r->eax, r->irq_number);
        // printf("+--------+--------+--------+--------+--------+--------+--------+--------+\n");

        // Display system halted message
        //printf("System Halted! IRQ Number: %d\n", r->irq_number);

        // Halt the system
        for (;;);
    }
}

// Define the type for exception handlers
typedef void (*ExceptionHandler)(Registers*);

// Declare an array to store handlers for each exception
ExceptionHandler exception_handlers[32];

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
    // Display system halted message
    printf("System Exception occured: %s\n\n", exception_messages[(int)r->irq_number]);
    while (1);
}

// Divide by zero handler (specific override)
void divide_by_zero_handler(Registers* r) {
    printf("Divide by zero exception caught!\n");
    // printf("Current context in handler: %p\n", (void*)current_try_context);

    if (current_try_context) {
        // printf("current_try_context: ESP=0x%X, EBP=0x%X, EIP=0x%X\n",
        //        current_try_context->esp, current_try_context->ebp, current_try_context->eip);
        throw(current_try_context, 1); // Throw the exception
    } else {
        //printf("No valid context. Halting.\n");
        while (1); // Halt the system
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
}

void exception_dispatcher(Registers* state) {
    // Look up the handler based on the IRQ number
    if (state->irq_number < 32) {
        exception_handlers[state->irq_number](state);  // Call the appropriate handler
    } else {
        // If the IRQ number is out of range, use a fallback
        generic_exception_handler(state);
    }
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

    setup_exceptions();
}
