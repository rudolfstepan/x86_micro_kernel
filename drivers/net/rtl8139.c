#include "rtl8139.h"

#include "arch/x86/include/sys.h"
#include "arch/x86/include/interrupt.h"
#include "drivers/bus/pci.h"
#include "drivers/char/io.h"
#include "drivers/net/netdev.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

#define RTL_IDR0          0x00
#define RTL_TSD0          0x10
#define RTL_TSAD0         0x20
#define RTL_RBSTART       0x30
#define RTL_CAPR          0x38
#define RTL_IMR           0x3C
#define RTL_ISR           0x3E
#define RTL_TCR           0x40
#define RTL_RCR           0x44
#define RTL_COMMAND       0x37

#define RTL_CMD_RESET     0x10
#define RTL_CMD_RX_ENABLE 0x08
#define RTL_CMD_TX_ENABLE 0x04
#define RTL_CMD_RX_EMPTY  0x01

#define RTL_ISR_ROK       0x0001
#define RTL_ISR_TOK       0x0004
#define RTL_ISR_RXOVW     0x0010

#define RTL_RX_ROK        0x0001
#define RTL_RCR_APM       0x00000002u
#define RTL_RCR_AM        0x00000004u
#define RTL_RCR_AB        0x00000008u
#define RTL_RCR_WRAP      0x00000080u
#define RTL_RCR_MXDMA_UNL (7u << 8)
#define RTL_RCR_RBLEN_64K (3u << 11)

#define RTL_TSD_OWN       (1u << 13)
#define RTL_TSD_TUN       (1u << 14)
#define RTL_TSD_TOK       (1u << 15)
#define RTL_TSD_TABT      (1u << 30)

#define RTL_TX_COUNT      4
#define RTL_TX_BUFFER_SIZE 2048
#define RTL_RX_RING_SIZE  (64u * 1024u)
#define RTL_RX_WRAP_SLACK 2048u
#define RTL_MAX_FRAME_SIZE 1518u

typedef struct {
    uint16_t io_base;
    uint8_t irq;
    uint8_t tx_next;
    uint32_t rx_offset;
    bool initialized;
} rtl8139_device_t;

static rtl8139_device_t rtl8139_device;
static volatile uint32_t rtl8139_tx_busy;
static volatile uint32_t rtl8139_rx_busy;
static volatile bool rtl8139_rx_pending;
static uint8_t rtl8139_tx_buffers[RTL_TX_COUNT][RTL_TX_BUFFER_SIZE]
    __attribute__((aligned(16)));
static uint8_t rtl8139_rx_buffer[RTL_RX_RING_SIZE + RTL_RX_WRAP_SLACK]
    __attribute__((aligned(16)));

static bool rtl8139_wait_reset(uint16_t base) {
    for (uint32_t timeout = 0; timeout < 1000000u; ++timeout) {
        if ((inb((uint16_t)(base + RTL_COMMAND)) & RTL_CMD_RESET) == 0) return true;
    }
    return false;
}

static bool rtl8139_hw_init(void) {
    uint16_t base = rtl8139_device.io_base;

    outb((uint16_t)(base + RTL_COMMAND), RTL_CMD_RESET);
    if (!rtl8139_wait_reset(base)) {
        printf("RTL8139: reset timeout\n");
        return false;
    }

    memset(rtl8139_rx_buffer, 0, sizeof(rtl8139_rx_buffer));
    memset(rtl8139_tx_buffers, 0, sizeof(rtl8139_tx_buffers));
    rtl8139_device.rx_offset = 0;
    rtl8139_device.tx_next = 0;

    outl((uint16_t)(base + RTL_RBSTART), (uint32_t)(uintptr_t)rtl8139_rx_buffer);
    outl((uint16_t)(base + RTL_RCR),
         RTL_RCR_APM | RTL_RCR_AM | RTL_RCR_AB | RTL_RCR_WRAP |
         RTL_RCR_MXDMA_UNL | RTL_RCR_RBLEN_64K);
    outl((uint16_t)(base + RTL_TCR), (3u << 24) | (7u << 8));
    /* Keep the device quiescent until its IRQ handler is installed. */
    outw((uint16_t)(base + RTL_IMR), 0);
    outw((uint16_t)(base + RTL_ISR), 0xFFFFu);
    return true;
}

static bool rtl8139_activate(void) {
    uint16_t base = rtl8139_device.io_base;
    rtl8139_device.initialized = true;
    outb((uint16_t)(base + RTL_COMMAND), RTL_CMD_RX_ENABLE | RTL_CMD_TX_ENABLE);

    uint8_t command = inb((uint16_t)(base + RTL_COMMAND));
    if ((command & (RTL_CMD_RX_ENABLE | RTL_CMD_TX_ENABLE)) !=
        (RTL_CMD_RX_ENABLE | RTL_CMD_TX_ENABLE)) {
        printf("RTL8139: RX/TX enable failed\n");
        rtl8139_device.initialized = false;
        return false;
    }

    outw((uint16_t)(base + RTL_ISR), 0xFFFFu);
    outw((uint16_t)(base + RTL_IMR),
         RTL_ISR_ROK | RTL_ISR_TOK | RTL_ISR_RXOVW);
    return true;
}

bool rtl8139_send_packet(void *data, uint16_t len) {
    if (netdev_outputs_fenced()) return false;
    if (!rtl8139_device.initialized || !data || len < 14 || len > RTL_MAX_FRAME_SIZE) {
        printf("RTL8139: invalid TX request (length=%u)\n", len);
        return false;
    }
    if (__sync_lock_test_and_set(&rtl8139_tx_busy, 1u)) {
        printf("RTL8139: another transmission is in progress\n");
        return false;
    }

    uint8_t slot = rtl8139_device.tx_next;
    uint16_t tsd_port = (uint16_t)(rtl8139_device.io_base + RTL_TSD0 + slot * 4u);

    /* Do not overwrite a buffer which the NIC can still be reading. */
    bool available = false;
    for (uint32_t timeout = 0; timeout < 1000000u; ++timeout) {
        uint32_t status = inl(tsd_port);
        if (status & (RTL_TSD_OWN | RTL_TSD_TOK | RTL_TSD_TABT)) {
            available = true;
            break;
        }
    }
    if (!available) {
        printf("RTL8139: TX descriptor %u is busy\n", slot);
        __sync_lock_release(&rtl8139_tx_busy);
        return false;
    }

    memcpy(rtl8139_tx_buffers[slot], data, len);
    __asm__ volatile("" ::: "memory");
    outl((uint16_t)(rtl8139_device.io_base + RTL_TSAD0 + slot * 4u),
         (uint32_t)(uintptr_t)rtl8139_tx_buffers[slot]);
    outl(tsd_port, len & 0x1FFFu);

    bool completed = false;
    for (uint32_t timeout = 0; timeout < 1000000u; ++timeout) {
        uint32_t status = inl(tsd_port);
        if (status & RTL_TSD_TOK) {
            completed = true;
            break;
        }
        if (status & (RTL_TSD_TABT | RTL_TSD_TUN)) break;
    }
    if (!completed) printf("RTL8139: TX timeout/error on descriptor %u\n", slot);

    rtl8139_device.tx_next = (uint8_t)((slot + 1u) % RTL_TX_COUNT);
    __sync_lock_release(&rtl8139_tx_busy);
    return completed;
}

void rtl8139_fence_outputs(void) {
    if (!rtl8139_device.initialized) return;
    outw((uint16_t)(rtl8139_device.io_base + RTL_IMR), 0);
    uint8_t command = inb((uint16_t)(rtl8139_device.io_base + RTL_COMMAND));
    outb((uint16_t)(rtl8139_device.io_base + RTL_COMMAND),
         (uint8_t)(command & (uint8_t)~RTL_CMD_TX_ENABLE));
}

bool rtl8139_outputs_fenced(void) {
    if (!rtl8139_device.initialized) return true;
    return (inb((uint16_t)(rtl8139_device.io_base + RTL_COMMAND)) &
            RTL_CMD_TX_ENABLE) == 0 &&
           inw((uint16_t)(rtl8139_device.io_base + RTL_IMR)) == 0;
}

static void rtl8139_drain_rx(void) {
    if (!rtl8139_device.initialized) return;

    uint16_t base = rtl8139_device.io_base;
    unsigned processed = 0;
    while ((inb((uint16_t)(base + RTL_COMMAND)) & RTL_CMD_RX_EMPTY) == 0 &&
           processed++ < 64u) {
        uint32_t offset = rtl8139_device.rx_offset;
        if (offset >= RTL_RX_RING_SIZE) offset = 0;

        const uint8_t *entry = rtl8139_rx_buffer + offset;
        uint16_t status = (uint16_t)(entry[0] | ((uint16_t)entry[1] << 8));
        uint16_t dma_length = (uint16_t)(entry[2] | ((uint16_t)entry[3] << 8));

        if ((status & RTL_RX_ROK) == 0 || dma_length < 4u ||
            dma_length > RTL_MAX_FRAME_SIZE + 4u) {
            printf("RTL8139: corrupt RX descriptor (status=0x%04X, length=%u)\n",
                   status, dma_length);
            outb((uint16_t)(base + RTL_COMMAND), 0);
            outb((uint16_t)(base + RTL_COMMAND), RTL_CMD_RX_ENABLE | RTL_CMD_TX_ENABLE);
            rtl8139_device.rx_offset = 0;
            outw((uint16_t)(base + RTL_CAPR), 0xFFF0u);
            rtl8139_rx_pending = false;
            return;
        }

        /* The DMA length includes the four-byte Ethernet FCS. */
        uint16_t frame_length = (uint16_t)(dma_length - 4u);
        netdev_deliver_rx(entry + 4, frame_length);

        offset = (offset + dma_length + 4u + 3u) & ~3u;
        if (offset >= RTL_RX_RING_SIZE) offset -= RTL_RX_RING_SIZE;
        rtl8139_device.rx_offset = offset;
        outw((uint16_t)(base + RTL_CAPR), (uint16_t)(offset - 16u));
    }
    rtl8139_rx_pending =
        (inb((uint16_t)(base + RTL_COMMAND)) & RTL_CMD_RX_EMPTY) == 0;
}

void rtl8139_receive_packet(void) {
    if (__sync_lock_test_and_set(&rtl8139_rx_busy, 1u)) {
        rtl8139_rx_pending = true;
        return;
    }
    rtl8139_drain_rx();
    __sync_lock_release(&rtl8139_rx_busy);
}

void rtl8139_poll_rx(void) {
    if (!rtl8139_device.initialized ||
        !__atomic_load_n(&rtl8139_rx_pending, __ATOMIC_ACQUIRE)) return;
    if (__sync_lock_test_and_set(&rtl8139_rx_busy, 1u)) return;

    uint16_t base = rtl8139_device.io_base;
    uint16_t saved_imr = inw((uint16_t)(base + RTL_IMR));
    outw((uint16_t)(base + RTL_IMR), 0);
    __atomic_store_n(&rtl8139_rx_pending, false, __ATOMIC_RELEASE);
    rtl8139_drain_rx();

    uint32_t flags = irq_save();
    __sync_lock_release(&rtl8139_rx_busy);
    outw((uint16_t)(base + RTL_IMR), saved_imr);
    irq_restore(flags);
}

void rtl8139_interrupt_handler(void) {
    if (!rtl8139_device.initialized) return;
    uint16_t port = (uint16_t)(rtl8139_device.io_base + RTL_ISR);
    uint16_t status = inw(port);
    if (status == 0 || status == 0xFFFFu) return;
    outw(port, status);

    if (status & (RTL_ISR_ROK | RTL_ISR_RXOVW)) {
        /* Hard IRQ only acknowledges and schedules the foreground drain. */
        __atomic_store_n(&rtl8139_rx_pending, true, __ATOMIC_RELEASE);
    }
}

void rtl8139_get_mac_address(uint8_t *mac) {
    if (!mac) return;
    if (!rtl8139_device.initialized) {
        memset(mac, 0, 6);
        return;
    }
    for (uint8_t i = 0; i < 6; ++i) {
        mac[i] = inb((uint16_t)(rtl8139_device.io_base + RTL_IDR0 + i));
    }
}

static int rtl8139_probe(pci_device_t *pci_dev) {
    if (!pci_dev || pci_dev->vendor_id != RTL8139_VENDOR_ID ||
        pci_dev->device_id != RTL8139_DEVICE_ID) return -1;

    uint32_t bar0 = pci_read_bar(pci_dev, 0);
    if ((bar0 & 1u) == 0) {
        printf("RTL8139: BAR0 is not an I/O BAR\n");
        return -1;
    }
    uint32_t io_base = bar0 & ~3u;
    if (io_base == 0 || io_base > 0xFFFFu) {
        printf("RTL8139: invalid I/O BAR 0x%08X\n", bar0);
        return -1;
    }

    uint8_t irq = pci_configure_irq(pci_dev);
    if (!pci_irq_is_valid(irq)) {
        printf("RTL8139: invalid legacy IRQ %u\n", irq);
        return -1;
    }

    pci_enable_device(pci_dev);
    pci_set_bus_master(pci_dev->bus, pci_dev->slot, pci_dev->function, 1);
    rtl8139_device.io_base = (uint16_t)io_base;
    rtl8139_device.irq = irq;

    if (!rtl8139_hw_init()) {
        pci_set_bus_master(pci_dev->bus, pci_dev->slot,
                           pci_dev->function, 0);
        rtl8139_device.io_base = 0;
        return -1;
    }

    if (register_interrupt_handler(irq, rtl8139_interrupt_handler) != 0) {
        pci_set_bus_master(pci_dev->bus, pci_dev->slot,
                           pci_dev->function, 0);
        rtl8139_device.io_base = 0;
        return -1;
    }
    if (!rtl8139_activate()) {
        outw((uint16_t)(rtl8139_device.io_base + RTL_IMR), 0);
        outb((uint16_t)(rtl8139_device.io_base + RTL_COMMAND), 0);
        pci_set_bus_master(pci_dev->bus, pci_dev->slot,
                           pci_dev->function, 0);
        rtl8139_device.io_base = 0;
        return -1;
    }

    uint8_t mac[6];
    rtl8139_get_mac_address(mac);
    printf("RTL8139 MAC: %02X:%02X:%02X:%02X:%02X:%02X, I/O: 0x%04X, IRQ: %u\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
           rtl8139_device.io_base, irq);
    return 0;
}

void rtl8139_detect(void) {
    printf("Detecting RTL8139 network card...\n");
    pci_register_driver(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID, rtl8139_probe);
}

int rtl8139_is_initialized(void) {
    return rtl8139_device.initialized;
}

void rtl8139_send_test_packet(void) {
    static uint8_t packet[60];
    uint8_t mac[6];
    rtl8139_get_mac_address(mac);
    memset(packet, 0, sizeof(packet));
    memset(packet, 0xFF, 6);
    memcpy(packet + 6, mac, 6);
    packet[12] = 0x08; packet[13] = 0x06;
    packet[14] = 0x00; packet[15] = 0x01;
    packet[16] = 0x08; packet[17] = 0x00;
    packet[18] = 6; packet[19] = 4;
    packet[20] = 0; packet[21] = 1;
    memcpy(packet + 22, mac, 6);
    packet[28] = 10; packet[29] = 0; packet[30] = 2; packet[31] = 15;
    packet[38] = 10; packet[39] = 0; packet[40] = 2; packet[41] = 1;
    rtl8139_send_packet(packet, sizeof(packet));
}
