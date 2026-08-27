/**
 * @file arch/x86/include/sys.h
 * @brief x86-GDT/IDT/Port- und CPU-Grundtypen.
 *
 * Layer: Ring-0 x86 architecture and memory.
 * Contract: Binärlayouts, Adressgrenzen und Privilegien entsprechen der x86-Hardware-ABI.
 * Safety: C-Strukturen entsprechen exakt den Assembler- und Hardwarelayouts.
 */
#ifndef SYS_H
#define SYS_H

#include <stddef.h>
#include <stdbool.h>
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

/* Keep the C view synchronized with the frames built by isr.asm, irq.asm and
 * syscall.asm.  A mismatch here makes exception return consume the wrong
 * words and is fatal before C diagnostics can help. */
_Static_assert(offsetof(Registers, irq_number) == 48,
               "Registers.irq_number offset must match assembly");
_Static_assert(offsetof(Registers, error_code) == 52,
               "Registers.error_code offset must match assembly");
_Static_assert(offsetof(Registers, eip) == 56,
               "Registers.eip offset must match assembly");
_Static_assert(sizeof(Registers) == 76,
               "Registers size must match a ring-transition frame");

//gdt
extern void gdt_install();
bool gdt_install_cpu(uint32_t cpu_index);

//idt
extern void idt_install();
extern void set_idt_entry(int vector, uint32_t handler);
extern void set_idt_entry_flags(int vector, uint32_t handler, uint8_t type_attr);
extern void set_idt_task_gate(int vector, uint16_t tss_selector);

//isr
extern void isr_install();

//irq
extern void irq_install();
extern void irq_handler(Registers* r);
extern int register_interrupt_handler(int irq, void* r);
extern int unregister_interrupt_handler(int irq, void* r);
int irq_set_affinity(uint8_t irq, uint32_t cpu_mask);
uint32_t irq_affinity_mask(uint8_t irq);
bool irq_affinity_bsp_only_ready(void);
void irq_uninstall_handler(int irq);
bool irq_pic_mask_line(uint8_t irq);
bool irq_pic_unmask_line(uint8_t irq);

extern volatile uint64_t cpu_frequency; // Global CPU frequency

static inline uint64_t cpu_cycle_counter_read(void) {
    uint32_t low;
    uint32_t high;
    __asm__ __volatile__("lfence\n\trdtsc\n\tlfence"
                         : "=a"(low), "=d"(high) : : "memory");
    return ((uint64_t)high << 32U) | low;
}


#endif
