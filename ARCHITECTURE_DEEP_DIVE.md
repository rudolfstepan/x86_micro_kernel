# x86 Microkernel - Low-Level Architecture Deep Dive

**Date**: 2025-11-07  
**Purpose**: Comprehensive explanation of GDT, IDT, ISR, IRQ, and Assembly-C integration  

---

## Table of Contents

1. [Boot Sequence Overview](#boot-sequence-overview)
2. [GDT (Global Descriptor Table)](#gdt-global-descriptor-table)
3. [IDT (Interrupt Descriptor Table)](#idt-interrupt-descriptor-table)
4. [ISR (Interrupt Service Routines)](#isr-interrupt-service-routines)
5. [IRQ (Hardware Interrupts)](#irq-hardware-interrupts)
6. [System Calls](#system-calls)
7. [Assembly-C Integration](#assembly-c-integration)
8. [Memory Protection](#memory-protection)

---

## Boot Sequence Overview

### 1. GRUB Loads Kernel (Multiboot)

**File**: `arch/x86/boot/multiboot.asm`

```asm
section .multiboot
align 4
multiboot_header:
    MODULEALIGN equ 1<<0    ; Align modules on page boundaries
    MEMINFO equ 1<<1        ; Request memory information
    FLAGS equ MODULEALIGN | MEMINFO
    MAGIC equ 0x1BADB002    ; Multiboot magic number
    CHECKSUM equ -(MAGIC + FLAGS)
    
    dd MAGIC
    dd FLAGS
    dd CHECKSUM
```

**What happens**:
1. GRUB reads this header at the start of kernel binary
2. Verifies magic number `0x1BADB002`
3. Loads kernel to memory address `0x00100000` (1MB mark)
4. Passes control to kernel entry point with:
   - `EAX` = Multiboot magic (`0x2BADB002`)
   - `EBX` = Physical address of Multiboot information structure

---

### 2. Kernel Entry Point

**File**: `kernel/init/kernel.c` → `kernel_main()`

```c
void kernel_main(unsigned long magic, unsigned long addr) {
    early_init();      // Stage 1: GDT, IDT, ISR, IRQ
    hardware_init();   // Stage 2: PIT timer, keyboard
    driver_init();     // Stage 3: VGA, ATA, PCI
    system_ready();    // Stage 4: Shell, command loop
}
```

---

## GDT (Global Descriptor Table)

### Purpose

The GDT defines **memory segments** and their **access rights** in protected mode. Without a properly configured GDT, the CPU cannot:
- Distinguish between kernel and user code
- Enforce memory protection
- Handle interrupts properly

---

### GDT Structure (C Code)

**File**: `arch/x86/cpu/gdt.c`

```c
// GDT Entry - 8 bytes
struct gdt_entry {
    unsigned short limit_low;    // Segment limit (bits 0-15)
    unsigned short base_low;     // Base address (bits 0-15)
    unsigned char base_middle;   // Base address (bits 16-23)
    unsigned char access;        // Access flags (present, privilege, type)
    unsigned char granularity;   // Granularity + limit (bits 16-19)
    unsigned char base_high;     // Base address (bits 24-31)
} __attribute__((packed));

// GDT Pointer - Loaded into GDTR register
struct gdt_ptr {
    unsigned short limit;        // Size of GDT - 1
    unsigned int base;           // Address of first GDT entry
} __attribute__((packed));

struct gdt_entry gdt[3];         // Our GDT: NULL, Code, Data
struct gdt_ptr gp;               // Pointer structure for LGDT
```

**Key Points**:
- `__attribute__((packed))` prevents compiler padding (must be exact size)
- 3 segments: NULL (required by x86), Code, Data

---

### GDT Setup Function

```c
void gdt_set_gate(int num, unsigned long base, unsigned long limit, 
                  unsigned char access, unsigned char gran) {
    // Split 32-bit base address into 3 parts
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;

    // Split 20-bit limit into 2 parts
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F);

    // Set granularity flags and access byte
    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access = access;
}

void gdt_install() {
    gp.limit = (sizeof(struct gdt_entry) * 3) - 1;
    gp.base = (unsigned)&gdt;

    // Entry 0: NULL segment (required by x86)
    gdt_set_gate(0, 0, 0, 0, 0);
    
    // Entry 1: Code segment (0x08)
    // Base=0, Limit=0xFFFFFFFF (4GB), Access=0x9A, Granularity=0xCF
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    
    // Entry 2: Data segment (0x10)
    // Base=0, Limit=0xFFFFFFFF (4GB), Access=0x92, Granularity=0xCF
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    gdt_flush();  // Call assembly to load GDT
}
```

**Access Byte Breakdown** (0x9A for code, 0x92 for data):
```
Bit 7: Present (1 = segment is present in memory)
Bit 6-5: Privilege (00 = ring 0 kernel, 11 = ring 3 user)
Bit 4: Descriptor type (1 = code/data, 0 = system)
Bit 3: Executable (1 = code, 0 = data)
Bit 2: Direction/Conforming
Bit 1: Readable/Writable
Bit 0: Accessed (set by CPU)

0x9A = 10011010 (Code: Present, Ring 0, Executable, Readable)
0x92 = 10010010 (Data: Present, Ring 0, Writable)
```

**Granularity Byte** (0xCF):
```
Bit 7: Granularity (1 = 4KB blocks, 0 = 1 byte blocks)
Bit 6: Size (1 = 32-bit, 0 = 16-bit)
Bit 5-4: Reserved (0)
Bit 3-0: Limit bits 16-19

0xCF = 11001111 (4KB granularity, 32-bit, limit bits = 1111)
```

---

### GDT Loading (Assembly)

**File**: `arch/x86/cpu/gdt.asm`

```asm
[BITS 32]
global gdt_flush
extern gp                       ; Import C variable

section .text
gdt_flush:
    lgdt [gp]                  ; Load GDT pointer into GDTR register
    mov ax, 0x10               ; 0x10 = data segment selector
    mov ds, ax                 ; Load data segment into DS
    mov es, ax                 ; Load into ES
    mov fs, ax                 ; Load into FS
    mov gs, ax                 ; Load into GS
    mov ss, ax                 ; Load into SS (stack segment)
    jmp 0x08:flush2            ; Far jump to code segment (0x08)
flush2:
    ret
```

**Why the far jump?**
- `jmp 0x08:flush2` loads CS (code segment) with 0x08
- Normal `jmp` doesn't reload CS
- Far jump forces CPU to reload CS from GDT
- Now all segments (CS, DS, ES, FS, GS, SS) point to correct GDT entries

**Segment Selectors**:
- `0x08` = Entry 1 in GDT (code segment): `index=1, TI=0, RPL=0`
- `0x10` = Entry 2 in GDT (data segment): `index=2, TI=0, RPL=0`
- Format: `[Index:13 bits][TI:1 bit][RPL:2 bits]`
  - Index: Which GDT entry (0-8191)
  - TI: Table Indicator (0=GDT, 1=LDT)
  - RPL: Requested Privilege Level (0-3)

---

## IDT (Interrupt Descriptor Table)

### Purpose

The IDT maps **interrupt numbers** to **handler functions**. When an interrupt occurs:
1. CPU looks up interrupt number in IDT
2. Finds handler address
3. Saves current state (pushes registers)
4. Jumps to handler
5. Handler processes interrupt
6. `IRET` instruction restores state and returns

---

### IDT Structure (C Code)

**File**: `arch/x86/cpu/idt.c`

```c
// IDT Entry - 8 bytes
struct idt_entry {
    uint16_t offset_low;       // Handler address (bits 0-15)
    uint16_t selector;         // Code segment selector (0x08)
    uint8_t zero;              // Always 0
    uint8_t type_attr;         // Type and attributes
    uint16_t offset_high;      // Handler address (bits 16-31)
} __attribute__((packed));

// IDT Pointer - Loaded into IDTR register
struct idt_ptr {
    unsigned short limit;      // Size of IDT - 1
    unsigned int base;         // Address of first IDT entry
} __attribute__((packed));

struct idt_entry idt[256];     // 256 interrupt vectors
struct idt_ptr idtp;
```

**Type/Attribute Byte** (0x8E for interrupt gate):
```
Bit 7: Present (1 = descriptor is valid)
Bit 6-5: Privilege level (00 = ring 0)
Bit 4: Storage segment (0 for interrupt/trap gates)
Bit 3-0: Gate type (1110 = 32-bit interrupt gate)

0x8E = 10001110 (Present, Ring 0, 32-bit interrupt gate)
```

---

### Setting IDT Entries

```c
void set_idt_entry(int vector, uint32_t handler) {
    idt[vector].offset_low = handler & 0xFFFF;
    idt[vector].selector = 0x08;      // Code segment from GDT
    idt[vector].zero = 0;
    idt[vector].type_attr = 0x8E;     // Interrupt gate
    idt[vector].offset_high = (handler >> 16) & 0xFFFF;
}

void idt_install() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (unsigned)&idt;
    memset((unsigned char*)&idt, 0, sizeof(struct idt_entry) * 256);
    
    idt_load();  // Call assembly to load IDT
}
```

---

### IDT Loading (Assembly)

**File**: `arch/x86/cpu/idt.asm`

```asm
[BITS 32]
global idt_load
extern idtp

section .text
idt_load:
    lidt [idtp]                ; Load IDT pointer into IDTR register
    ret
```

**Simple but critical**: `LIDT` instruction loads IDT base and limit into CPU's IDTR register.

---

## ISR (Interrupt Service Routines)

### Purpose

ISRs handle **CPU exceptions** (interrupts 0-31):
- Divide by zero (0)
- Debug (1)
- Page fault (14)
- General protection fault (13)
- etc.

---

### ISR Assembly Stubs

**File**: `arch/x86/cpu/isr.asm`

Each ISR has dedicated assembly stub:

```asm
; Exception 0: Divide By Zero
isr0:
    cli                        ; Disable interrupts
    push byte 0                ; Push dummy error code (CPU doesn't push one)
    push byte 0                ; Push interrupt number
    jmp isr_common_stub        ; Jump to common handler

; Exception 8: Double Fault (CPU pushes error code automatically)
isr8:
    cli
    push byte 8                ; Push interrupt number (no dummy code needed)
    jmp isr_common_stub

; ... (isr1-isr31 similar pattern)
```

**Key Points**:
- Some exceptions push error code automatically (8, 10-14, 17)
- Others don't, so we push dummy 0 for consistent stack layout
- All jump to `isr_common_stub` which saves state and calls C handler

---

### Common ISR Handler (Assembly)

```asm
isr_common_stub:
    pusha                      ; Push all general-purpose registers
                               ; (EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI)
    push ds                    ; Push data segment
    push es                    ; Push extra segment
    push fs                    ; Push FS segment
    push gs                    ; Push GS segment
    
    mov ax, 0x10               ; Load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    mov eax, esp               ; ESP points to saved registers
    push eax                   ; Pass pointer to C handler
    
    call exception_dispatcher  ; Call C function
    
    add esp, 4                 ; Clean up stack (remove pushed pointer)
    pop gs                     ; Restore segments
    pop fs
    pop es
    pop ds
    popa                       ; Restore general-purpose registers
    add esp, 8                 ; Remove error code and interrupt number
    iret                       ; Return from interrupt (pops EIP, CS, EFLAGS)
```

**Stack Layout After `push eax`**:
```
[ESP] -> Pointer to Registers structure
         +0: GS
         +4: FS
         +8: ES
         +12: DS
         +16: EDI
         +20: ESI
         +24: EBP
         +28: ESP (original)
         +32: EBX
         +36: EDX
         +40: ECX
         +44: EAX
         +48: IRQ number
         +52: Error code
         +56: EIP (pushed by CPU)
         +60: CS (pushed by CPU)
         +64: EFLAGS (pushed by CPU)
```

---

### ISR C Handler

**File**: `arch/x86/cpu/isr.c`

```c
// Exception names
const char* exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    // ... (32 total)
};

// Handler function type
typedef void (*ExceptionHandler)(Registers*);

// Array of custom exception handlers
ExceptionHandler exception_handlers[32];

// Generic exception handler
void generic_exception_handler(Registers* r) {
    printf("System Exception: %s\n", exception_messages[r->irq_number]);
    while (1);  // Halt
}

// Divide by zero handler (custom)
void divide_by_zero_handler(Registers* r) {
    printf("Divide by zero exception caught!\n");
    while (1);  // Halt
}

// Dispatcher called from assembly
void exception_dispatcher(Registers* r) {
    if (r->irq_number < 32 && exception_handlers[r->irq_number]) {
        exception_handlers[r->irq_number](r);
    } else {
        generic_exception_handler(r);
    }
}

// Setup function - install custom handlers
void setup_exceptions() {
    for (int i = 0; i < 32; i++) {
        exception_handlers[i] = generic_exception_handler;
    }
    exception_handlers[0] = divide_by_zero_handler;  // Custom handler
}

// Install all ISRs into IDT
void isr_install() {
    set_idt_entry(0, (uint32_t)isr0);
    set_idt_entry(1, (uint32_t)isr1);
    // ... (set all 32 ISRs)
    set_idt_entry(31, (uint32_t)isr31);
    
    setup_exceptions();
}
```

---

## IRQ (Hardware Interrupts)

### Purpose

IRQs handle **hardware interrupts** from devices:
- IRQ 0: Timer (PIT)
- IRQ 1: Keyboard
- IRQ 6: Floppy disk
- IRQ 14: Primary ATA hard disk
- etc.

---

### PIC (Programmable Interrupt Controller)

x86 systems have 2 PICs (master and slave) cascaded together:
- **Master PIC**: IRQ 0-7 (ports 0x20, 0x21)
- **Slave PIC**: IRQ 8-15 (ports 0xA0, 0xA1)

**Problem**: By default, PICs map to interrupts 0-15, **conflicting with CPU exceptions** (0-31).

**Solution**: **Remap** PICs to interrupts 32-47.

---

### IRQ Remapping

**File**: `arch/x86/cpu/irq.c`

```c
void irq_remap(void) {
    // Send initialization command to both PICs
    outb(0x20, 0x11);          // Master PIC: ICW1 (Init + ICW4 needed)
    outb(0xA0, 0x11);          // Slave PIC: ICW1
    
    // Set vector offsets
    outb(0x21, 0x20);          // Master PIC: ICW2 (offset to 0x20 = 32)
    outb(0xA1, 0x28);          // Slave PIC: ICW2 (offset to 0x28 = 40)
    
    // Tell PICs how they're cascaded
    outb(0x21, 0x04);          // Master: Slave on IRQ2
    outb(0xA1, 0x02);          // Slave: Cascade identity
    
    // Set 8086 mode
    outb(0x21, 0x01);          // Master: 8086/88 mode
    outb(0xA1, 0x01);          // Slave: 8086/88 mode
    
    // Unmask all interrupts
    outb(0x21, 0x0);           // Master: Enable all IRQs
    outb(0xA1, 0x0);           // Slave: Enable all IRQs
}
```

**After remapping**:
- IRQ 0 → Interrupt 32 (0x20)
- IRQ 1 → Interrupt 33 (0x21)
- ...
- IRQ 15 → Interrupt 47 (0x2F)

---

### IRQ Assembly Stubs

**File**: `arch/x86/cpu/irq.asm`

```asm
; IRQ 0 (Timer) → Interrupt 32
irq0:
    cli
    push byte 0                ; Dummy error code
    push byte 32               ; IRQ number (32 = IRQ 0)
    jmp irq_common_stub

; IRQ 1 (Keyboard) → Interrupt 33
irq1:
    cli
    push byte 0
    push byte 33               ; IRQ number (33 = IRQ 1)
    jmp irq_common_stub

; ... (irq2-irq15 similar)

irq_common_stub:
    pusha                      ; Save all registers
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10               ; Load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov eax, esp               ; Pass pointer to registers
    push eax
    
    call irq_handler           ; Call C handler
    
    pop eax                    ; Clean up
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8                 ; Remove error code and IRQ number
    iret                       ; Return from interrupt
```

---

### IRQ C Handler

**File**: `arch/x86/cpu/irq.c`

```c
// Array of custom IRQ handlers
void* irq_routines[17] = { 0 };

// Register custom handler for specific IRQ
void register_interrupt_handler(int irq, void* handler) {
    irq_routines[irq] = handler;
}

// Main IRQ handler dispatcher
void irq_handler(Registers* regs) {
    // Calculate IRQ number (subtract 32 from interrupt number)
    int irq = regs->irq_number - 32;
    
    // Call custom handler if registered
    if (irq_routines[irq]) {
        void (*handler)(Registers*) = (void (*)(Registers*))(irq_routines[irq]);
        handler(regs);
    }
    
    // Send EOI (End Of Interrupt) to PIC
    if (regs->irq_number >= 40) {
        outb(0xA0, 0x20);      // Send EOI to slave PIC (for IRQ 8-15)
    }
    outb(0x20, 0x20);          // Always send EOI to master PIC
}

// Install all IRQs into IDT
void irq_install() {
    irq_remap();               // Remap PICs first
    
    set_idt_entry(0x20, (uint32_t)irq0);   // Timer
    set_idt_entry(0x21, (uint32_t)irq1);   // Keyboard
    set_idt_entry(0x22, (uint32_t)irq2);   // Cascade
    // ... (set all 16 IRQs)
    set_idt_entry(0x2F, (uint32_t)irq15);  // Secondary ATA
}
```

---

### Example: Keyboard IRQ Handler

**File**: `drivers/char/kb.c`

```c
void keyboard_handler(Registers* r) {
    unsigned char scancode = inb(0x60);  // Read from keyboard port
    
    // Process scancode...
    if (!(scancode & 0x80)) {  // Key press (not release)
        char c = scancode_to_char(scancode);
        input_queue_push(c);
    }
}

void kb_install() {
    register_interrupt_handler(1, keyboard_handler);  // IRQ 1 = keyboard
}
```

**Flow**:
1. User presses key
2. Keyboard controller triggers IRQ 1
3. PIC signals CPU on interrupt line
4. CPU looks up interrupt 33 in IDT
5. Jumps to `irq1` assembly stub
6. Assembly saves state, calls `irq_handler()`
7. `irq_handler()` calls `keyboard_handler()`
8. Handler reads scancode, processes it
9. Returns through assembly stub
10. `IRET` restores state, continues execution

---

## System Calls

### Purpose

System calls allow **user programs** to request **kernel services** without direct access to privileged operations.

---

### System Call Mechanism

**File**: `arch/x86/cpu/syscall.asm`

```asm
[BITS 32]
global syscall_handler_asm
extern syscall_handler

section .text
syscall_handler_asm:
    sti                        ; Enable interrupts during syscall
    call syscall_handler       ; Call C handler
    cli                        ; Disable interrupts before return
    iretd                      ; Return from interrupt
```

**Registered as interrupt 0x80**:
```c
set_idt_entry(0x80, (uint32_t)syscall_handler_asm);
```

---

### System Call Table

**File**: `kernel/syscall/syscall_table.c`

```c
// Function pointers for each syscall
void* syscall_table[512] = {
    (void*)&display_putchar,            // Syscall 0
    (void*)&kernel_print_number,        // Syscall 1
    (void*)&pit_delay,                  // Syscall 2
    (void*)&kb_wait_enter,              // Syscall 3
    (void*)&k_malloc,                   // Syscall 4
    (void*)&k_free,                     // Syscall 5
    (void*)&k_realloc,                  // Syscall 6
    (void*)&getchar,                    // Syscall 7
    (void*)&register_interrupt_handler, // Syscall 8
};

void syscall_handler(void* irq_number) {
    int syscall_index, arg1, arg2, arg3;
    
    // Read registers
    __asm__ __volatile__(
        ""
        : "=a"(syscall_index),  // EAX = syscall number
          "=b"(arg1),           // EBX = argument 1
          "=c"(arg2),           // ECX = argument 2
          "=d"(arg3)            // EDX = argument 3
    );
    
    // Validate and dispatch
    if (syscall_index >= 0 && syscall_index < 512 && syscall_table[syscall_index]) {
        void* func_ptr = syscall_table[syscall_index];
        
        // Call with appropriate argument count
        switch (syscall_index) {
            case 0:  // No arguments
                ((void (*)(void))func_ptr)();
                break;
            case 1:  // One argument
                ((void (*)(int))func_ptr)(arg1);
                break;
            // ... etc
        }
    }
}
```

---

### User Program Making Syscall

**Example**: User program calls `putchar()`

```c
// lib/libc/stdio.c
void putchar(char c) {
    // Put syscall number in EAX, argument in EBX
    __asm__ __volatile__(
        "mov $0, %%eax\n"      // Syscall 0 = display_putchar
        "mov %0, %%ebx\n"      // Argument = character
        "int $0x80"            // Trigger interrupt 0x80
        : 
        : "r"(c)
        : "eax", "ebx"
    );
}
```

**Flow**:
1. User program calls `putchar('A')`
2. Function puts 0 in EAX, 'A' in EBX
3. `INT 0x80` triggers interrupt
4. CPU looks up interrupt 0x80 in IDT
5. Jumps to `syscall_handler_asm`
6. Assembly calls `syscall_handler()`
7. C handler reads EAX (0), EBX ('A')
8. Looks up `syscall_table[0]` = `display_putchar`
9. Calls `display_putchar('A')`
10. Returns through `IRET`
11. User program continues

---

## Assembly-C Integration

### How Assembly and C Work Together

#### 1. **External Declarations**

**C declares assembly functions**:
```c
extern void gdt_flush();        // Defined in gdt.asm
extern void idt_load();         // Defined in idt.asm
extern void isr0();             // Defined in isr.asm
```

**Assembly declares C variables/functions**:
```asm
extern gp                       ; C variable from gdt.c
extern idtp                     ; C variable from idt.c
extern irq_handler              ; C function from irq.c
```

---

#### 2. **Global Symbols**

Assembly exports symbols with `global`:
```asm
global gdt_flush
global idt_load
global isr0, isr1, isr2, ...
```

C exports symbols automatically (no `static`):
```c
struct gdt_ptr gp;              // Exported, accessible from assembly
void irq_handler(Registers* r); // Exported, callable from assembly
```

---

#### 3. **Calling Convention (cdecl)**

**C calls assembly**:
- Arguments pushed right-to-left on stack
- Caller cleans up stack
- Return value in EAX

**Assembly calls C**:
```asm
push eax                        ; Push argument
call c_function                 ; Call C function
add esp, 4                      ; Clean up stack (remove argument)
```

**C function signature**:
```c
void c_function(uint32_t arg);  // Receives argument from stack
```

---

#### 4. **Register Preservation**

**Caller-saved** (function can modify):
- EAX, ECX, EDX

**Callee-saved** (function must preserve):
- EBX, ESI, EDI, EBP, ESP

Assembly must save/restore callee-saved registers:
```asm
push ebx                        ; Save
push esi
push edi
; ... do work ...
pop edi                         ; Restore
pop esi
pop ebx
```

---

#### 5. **Stack Frame Layout**

When C function is called:
```
[ESP+0]  Return address (pushed by CALL)
[ESP+4]  Argument 1
[ESP+8]  Argument 2
[ESP+12] Argument 3
```

Assembly accesses arguments:
```asm
mov eax, [esp+4]                ; Get first argument
mov ebx, [esp+8]                ; Get second argument
```

---

### Example: Complete Flow

**C code** (`kernel/init/kernel.c`):
```c
void early_init() {
    gdt_install();              // C function
    idt_install();              // C function
    isr_install();              // C function
    irq_install();              // C function
}

void gdt_install() {
    gp.limit = (sizeof(struct gdt_entry) * 3) - 1;
    gp.base = (unsigned)&gdt;
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdt_flush();                // Call assembly
}
```

**Assembly code** (`arch/x86/cpu/gdt.asm`):
```asm
extern gp                       ; C variable

gdt_flush:
    lgdt [gp]                  ; Load GDT from C variable
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:flush2
flush2:
    ret                        ; Return to C
```

**Flow**:
1. C calls `gdt_flush()`
2. CPU pushes return address on stack
3. Jumps to `gdt_flush` in assembly
4. Assembly executes `lgdt`, segment loads, far jump
5. Assembly executes `ret`
6. CPU pops return address, jumps back to C
7. C continues execution

---

## Memory Protection

### Privilege Levels (Rings)

x86 has 4 privilege levels:
- **Ring 0**: Kernel (highest privilege)
- **Ring 1**: Device drivers (rarely used)
- **Ring 2**: Device drivers (rarely used)
- **Ring 3**: User programs (lowest privilege)

Our kernel uses **Ring 0** for kernel, **Ring 3** for user programs.

---

### GDT Segments Define Privileges

**Code segment** (Ring 0):
```
Access byte = 0x9A = 10011010
              ^^
              Ring 0 (bits 6-5 = 00)
```

**Data segment** (Ring 0):
```
Access byte = 0x92 = 10010010
              ^^
              Ring 0 (bits 6-5 = 00)
```

**For user segments**, change bits 6-5 to `11`:
```
0x9A → 0xFA (Ring 3 code)
0x92 → 0xF2 (Ring 3 data)
```

---

### Protected Instructions

**Ring 0 only**:
- `CLI` (Clear Interrupts)
- `STI` (Set Interrupts)
- `HLT` (Halt CPU)
- `LGDT` (Load GDT)
- `LIDT` (Load IDT)
- `IN`/`OUT` (I/O ports)

**Ring 3 attempt → General Protection Fault (Exception 13)**

---

### System Call Bridge

System calls allow Ring 3 programs to request Ring 0 services:
```
User Program (Ring 3)
    |
    | INT 0x80 (syscall)
    v
IDT Lookup → Interrupt Gate
    |
    | CPU switches to Ring 0
    v
Kernel Handler (Ring 0)
    |
    | Process request
    v
IRET (return to Ring 3)
    |
    v
User Program (Ring 3)
```

**Key**: Interrupt gates automatically switch to Ring 0, but only through IDT (controlled by kernel).

---

## Summary Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                     BOOT SEQUENCE                               │
├─────────────────────────────────────────────────────────────────┤
│ 1. GRUB loads kernel (Multiboot)                                │
│ 2. Jump to kernel_main()                                        │
│ 3. early_init():                                                │
│    ├─ gdt_install() → GDT (segments)                            │
│    ├─ idt_install() → IDT (interrupt table)                     │
│    ├─ isr_install() → ISR 0-31 (CPU exceptions)                 │
│    └─ irq_install() → IRQ 32-47 (hardware interrupts)           │
│ 4. hardware_init() → PIT timer, keyboard                        │
│ 5. driver_init() → VGA, ATA, PCI                                │
│ 6. system_ready() → Shell                                       │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                   INTERRUPT HANDLING                            │
├─────────────────────────────────────────────────────────────────┤
│ Interrupt Occurs (Timer, Keyboard, Exception, Syscall)         │
│         ↓                                                        │
│ CPU looks up vector in IDT                                      │
│         ↓                                                        │
│ Jump to Assembly stub (isr0, irq1, syscall_handler_asm)        │
│         ↓                                                        │
│ Save all registers (PUSHA, push segments)                       │
│         ↓                                                        │
│ Switch to kernel data segment (mov ax, 0x10)                    │
│         ↓                                                        │
│ Pass register pointer to C handler                              │
│         ↓                                                        │
│ Call C function (exception_dispatcher, irq_handler, etc.)       │
│         ↓                                                        │
│ C processes interrupt                                           │
│         ↓                                                        │
│ Return to assembly                                              │
│         ↓                                                        │
│ Restore segments and registers (pop, POPA)                      │
│         ↓                                                        │
│ Send EOI to PIC (for IRQs)                                      │
│         ↓                                                        │
│ IRET (return from interrupt)                                    │
│         ↓                                                        │
│ Resume execution                                                │
└─────────────────────────────────────────────────────────────────┘
```

---

## Key Takeaways

1. **GDT defines memory segments** - Must be set up before protected mode works properly

2. **IDT maps interrupts to handlers** - CPU uses this table for all interrupts

3. **Assembly stubs save state** - Critical for preserving program execution context

4. **C handlers process logic** - High-level code for interrupt handling

5. **PICs must be remapped** - Avoid conflict with CPU exceptions

6. **System calls bridge user/kernel** - Controlled way for Ring 3 to access Ring 0

7. **IRET restores everything** - Special instruction for returning from interrupts

8. **extern/global links assembly-C** - Allows calling functions across languages

9. **Stack is key communication** - Arguments, return addresses, saved state

10. **Memory protection enforced by CPU** - Rings prevent user programs from crashing kernel

---

## Files Reference

**Assembly Files**:
- `arch/x86/boot/multiboot.asm` - Multiboot header
- `arch/x86/cpu/gdt.asm` - GDT loading
- `arch/x86/cpu/idt.asm` - IDT loading
- `arch/x86/cpu/isr.asm` - Exception handlers (0-31)
- `arch/x86/cpu/irq.asm` - Hardware interrupt handlers (32-47)
- `arch/x86/cpu/syscall.asm` - System call handler (0x80)

**C Files**:
- `arch/x86/cpu/gdt.c` - GDT setup
- `arch/x86/cpu/idt.c` - IDT setup
- `arch/x86/cpu/isr.c` - Exception dispatcher
- `arch/x86/cpu/irq.c` - IRQ dispatcher, PIC remapping
- `kernel/syscall/syscall_table.c` - System call table

**Header Files**:
- `arch/x86/include/sys.h` - Registers structure, function declarations

---

**This is professional-grade OS architecture!** The separation of concerns, clean assembly-C interface, and proper interrupt handling show excellent system design. 🎉
