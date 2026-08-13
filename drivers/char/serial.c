#include "serial.h"
#include "io.h"
#include "kb.h"
#include "arch/x86/include/interrupt.h"
#include "arch/x86/include/sys.h"

//=============================================================================
// SERIAL PORT REGISTERS
//=============================================================================

#define SERIAL_DATA(base)          (base)
#define SERIAL_INT_ENABLE(base)    (base + 1)
#define SERIAL_FIFO_CTRL(base)     (base + 2)
#define SERIAL_INT_IDENT(base)     (base + 2)
#define SERIAL_LINE_CTRL(base)     (base + 3)
#define SERIAL_MODEM_CTRL(base)    (base + 4)
#define SERIAL_LINE_STATUS(base)   (base + 5)
#define SERIAL_MODEM_STATUS(base)  (base + 6)

// Interrupt Enable Register bits
#define SERIAL_IER_RX_AVAILABLE    0x01
#define SERIAL_IER_TX_EMPTY        0x02

// Interrupt Identification Register bits and causes
#define SERIAL_IIR_NO_PENDING      0x01
#define SERIAL_IIR_CAUSE_MASK      0x0E
#define SERIAL_IIR_MODEM_STATUS    0x00
#define SERIAL_IIR_TX_EMPTY        0x02
#define SERIAL_IIR_RX_AVAILABLE    0x04
#define SERIAL_IIR_LINE_STATUS     0x06
#define SERIAL_IIR_RX_TIMEOUT      0x0C

// Line Status Register bits
#define SERIAL_LSR_DATA_READY      0x01
#define SERIAL_LSR_TRANSMIT_EMPTY  0x20

#define SERIAL_RX_RING_SIZE        512u
#define SERIAL_IRQ_DRAIN_LIMIT     SERIAL_RX_RING_SIZE
#define SERIAL_IRQ_CAUSE_LIMIT     16u

static volatile uint8_t serial_rx_ring[SERIAL_RX_RING_SIZE];
static volatile uint16_t serial_rx_head;
static volatile uint16_t serial_rx_tail;
static volatile uint32_t serial_rx_dropped;
static volatile bool serial_rx_irq_active;

static bool serial_rx_ring_push(uint8_t value) {
    uint16_t head = serial_rx_head;
    uint16_t next = (uint16_t)((head + 1u) % SERIAL_RX_RING_SIZE);
    if (next == serial_rx_tail) {
        ++serial_rx_dropped;
        return false;
    }

    serial_rx_ring[head] = value;
    __asm__ __volatile__("" ::: "memory");
    serial_rx_head = next;
    return true;
}

static bool serial_rx_ring_pop(char *value) {
    uint16_t tail = serial_rx_tail;
    if (!value || tail == serial_rx_head) return false;

    *value = (char)serial_rx_ring[tail];
    __asm__ __volatile__("" ::: "memory");
    serial_rx_tail =
        (uint16_t)((tail + 1u) % SERIAL_RX_RING_SIZE);
    return true;
}

/* Called only with interrupts disabled (either from IRQ4 or an irq_save()
 * critical section).  Always consume hardware bytes, even when the software
 * ring is full, so the UART interrupt source cannot remain asserted. */
static bool serial_drain_com1_rx(void) {
    bool published = false;
    for (unsigned int drained = 0; drained < SERIAL_IRQ_DRAIN_LIMIT;
         ++drained) {
        uint8_t status = inb(SERIAL_LINE_STATUS(SERIAL_COM1));
        if ((status & SERIAL_LSR_DATA_READY) == 0) break;
        if (serial_rx_ring_push(inb(SERIAL_DATA(SERIAL_COM1)))) {
            published = true;
        }
    }
    return published;
}

static void serial_com1_irq_handler(Registers *regs) {
    (void)regs;

    for (unsigned int handled = 0; handled < SERIAL_IRQ_CAUSE_LIMIT;
         ++handled) {
        uint8_t identification = inb(SERIAL_INT_IDENT(SERIAL_COM1));
        if (identification & SERIAL_IIR_NO_PENDING) return;

        switch (identification & SERIAL_IIR_CAUSE_MASK) {
            case SERIAL_IIR_RX_AVAILABLE:
            case SERIAL_IIR_RX_TIMEOUT:
            case SERIAL_IIR_LINE_STATUS:
                if (serial_drain_com1_rx()) kb_notify_input_ready();
                break;
            case SERIAL_IIR_MODEM_STATUS:
                (void)inb(SERIAL_MODEM_STATUS(SERIAL_COM1));
                break;
            case SERIAL_IIR_TX_EMPTY: {
                /* TX interrupts are not used.  Mask one if another component
                 * enabled it accidentally so IRQ4 cannot spin forever. */
                uint8_t enabled = inb(SERIAL_INT_ENABLE(SERIAL_COM1));
                outb(SERIAL_INT_ENABLE(SERIAL_COM1),
                     (uint8_t)(enabled & ~SERIAL_IER_TX_EMPTY));
                break;
            }
            default:
                return;
        }
    }
}

//=============================================================================
// INITIALIZATION
//=============================================================================

/**
 * Initialize a serial port
 * @param port Base I/O port address (e.g., SERIAL_COM1)
 */
void serial_init(uint16_t port) {
    outb(SERIAL_INT_ENABLE(port), 0x00);    // Disable interrupts
    outb(SERIAL_LINE_CTRL(port), 0x80);     // Enable DLAB (set baud rate divisor)
    outb(SERIAL_DATA(port), 0x01);          // Set divisor to 1 (115200 baud)
    outb(SERIAL_INT_ENABLE(port), 0x00);    // High byte of divisor
    outb(SERIAL_LINE_CTRL(port), 0x03);     // 8 bits, no parity, one stop bit
    // Enable and clear both FIFOs. Bits 6-7 remain zero for a one-byte RX
    // trigger, which is appropriate for interactive console input.
    outb(SERIAL_FIFO_CTRL(port), 0x07);
    outb(SERIAL_MODEM_CTRL(port), 0x0B);    // IRQs enabled, RTS/DSR set
}

/**
 * Initialize COM1 as default serial port (for console)
 */
void serial_init_default(void) {
    serial_init(SERIAL_COM1);
}

bool serial_install_rx_irq(void) {
    uint32_t flags = irq_save();
    if (serial_rx_irq_active) {
        irq_restore(flags);
        return true;
    }

    // Keep the UART source masked until IRQ4 has a registered owner.
    outb(SERIAL_INT_ENABLE(SERIAL_COM1), 0x00);
    if (register_interrupt_handler(4, (void *)serial_com1_irq_handler) != 0) {
        irq_restore(flags);
        return false;
    }

    // Preserve any bytes which arrived after the early UART initialization.
    // Writing 0x01 selects the one-byte trigger without clearing the FIFO.
    outb(SERIAL_FIFO_CTRL(SERIAL_COM1), 0x01);
    (void)serial_drain_com1_rx();
    serial_rx_irq_active = true;
    __asm__ __volatile__("" ::: "memory");
    outb(SERIAL_INT_ENABLE(SERIAL_COM1), SERIAL_IER_RX_AVAILABLE);

    irq_restore(flags);
    return true;
}

//=============================================================================
// STATUS CHECKS
//=============================================================================

/**
 * Check if data is available to read
 * @param port Base I/O port address
 * @return true if data is available
 */
bool serial_received(uint16_t port) {
    if (port == SERIAL_COM1 && serial_rx_irq_active) {
        uint32_t flags = irq_save();
        if (serial_drain_com1_rx()) kb_notify_input_ready();
        bool available = serial_rx_tail != serial_rx_head;
        irq_restore(flags);
        return available;
    }
    return (inb(SERIAL_LINE_STATUS(port)) & SERIAL_LSR_DATA_READY) != 0;
}

/**
 * Check if transmit buffer is empty
 * @param port Base I/O port address
 * @return true if ready to transmit
 */
bool serial_is_transmit_empty(uint16_t port) {
    return inb(SERIAL_LINE_STATUS(port)) & SERIAL_LSR_TRANSMIT_EMPTY;
}

//=============================================================================
// WRITE OPERATIONS
//=============================================================================

/**
 * Write a single character to serial port
 * @param port Base I/O port address
 * @param ch Character to write
 */
void serial_write_char(uint16_t port, char ch) {
    // Wait for transmit buffer to be empty
    while (!serial_is_transmit_empty(port));
    
    // Send character
    outb(SERIAL_DATA(port), ch);
}

/**
 * Write a null-terminated string to serial port
 * @param port Base I/O port address
 * @param str String to write
 */
void serial_write_string(uint16_t port, const char* str) {
    while (*str) {
        serial_write_char(port, *str++);
    }
}

//=============================================================================
// READ OPERATIONS
//=============================================================================

/**
 * Read a character from serial port (non-blocking)
 * @param port Base I/O port address
 * @return Character read, or 0 if no data available
 */
char serial_read_char(uint16_t port) {
    if (port == SERIAL_COM1 && serial_rx_irq_active) {
        char value = 0;
        uint32_t flags = irq_save();
        // This also covers a byte which arrived immediately before irq_save().
        if (serial_drain_com1_rx()) kb_notify_input_ready();
        bool available = serial_rx_ring_pop(&value);
        irq_restore(flags);
        return available ? value : 0;
    }
    if (inb(SERIAL_LINE_STATUS(port)) & SERIAL_LSR_DATA_READY) {
        return (char)inb(SERIAL_DATA(port));
    }
    return 0;
}
