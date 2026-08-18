#include "rtl8168.h"

#include "arch/x86/include/interrupt.h"
#include "drivers/bus/pci.h"
#include "drivers/net/netdev.h"
#include "include/kernel/panic.h"
#include "kernel/time/pit.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RTL8168_VENDOR_ID 0x10ECU
#define RTL8168_DEVICE_ID 0x8168U

#define RTL8168_MMIO_SIZE 0x1000U
#define RTL8168_TX_RING_COUNT 8U
#define RTL8168_RX_RING_COUNT 16U
#define RTL8168_RX_BUFFER_SIZE 2048U
#define RTL8168_MAX_FRAME_SIZE 1518U
#define RTL8168_POLL_LIMIT 1000000U

/* Register layout shared by RTL8168/8111 gigabit controllers. */
#define RTL8168_MAC0 0x00U
#define RTL8168_TX_DESC_LOW 0x20U
#define RTL8168_TX_DESC_HIGH 0x24U
#define RTL8168_CHIP_COMMAND 0x37U
#define RTL8168_TX_POLL 0x38U
#define RTL8168_INTERRUPT_MASK 0x3CU
#define RTL8168_INTERRUPT_STATUS 0x3EU
#define RTL8168_TX_CONFIG 0x40U
#define RTL8168_RX_CONFIG 0x44U
#define RTL8168_PHY_STATUS 0x6CU
#define RTL8168_CONFIG_1 0x52U
#define RTL8168_RX_MAX_SIZE 0xDAU
#define RTL8168_CPLUS_COMMAND 0xE0U
#define RTL8168_RX_DESC_LOW 0xE4U
#define RTL8168_RX_DESC_HIGH 0xE8U

#define RTL8168_CMD_RESET 0x10U
#define RTL8168_CMD_RX_ENABLE 0x08U
#define RTL8168_CMD_TX_ENABLE 0x04U
#define RTL8168_PHY_LINK_STATUS 0x02U

#define RTL8168_INT_RX_OK 0x0001U
#define RTL8168_INT_RX_ERROR 0x0002U
#define RTL8168_INT_TX_OK 0x0004U
#define RTL8168_INT_TX_ERROR 0x0008U
#define RTL8168_INT_RX_OVERFLOW 0x0010U
#define RTL8168_INT_LINK_CHANGE 0x0020U
#define RTL8168_INT_RX_FIFO_OVERFLOW 0x0040U
#define RTL8168_INT_TX_UNAVAILABLE 0x0080U
#define RTL8168_INT_SYSTEM_ERROR 0x8000U

#define RTL8168_RX_ACCEPT_ERROR 0x20U
#define RTL8168_RX_ACCEPT_RUNT 0x10U
#define RTL8168_RX_ACCEPT_BROADCAST 0x08U
#define RTL8168_RX_ACCEPT_MULTICAST 0x04U
#define RTL8168_RX_ACCEPT_PHYSICAL_MATCH 0x02U
#define RTL8168_RX_DMA_BURST (7U << 8)
#define RTL8168_RX_FIFO_THRESHOLD (7U << 13)

#define RTL8168_DESC_OWN (1U << 31)
#define RTL8168_DESC_RING_END (1U << 30)
#define RTL8168_DESC_FIRST_FRAGMENT (1U << 29)
#define RTL8168_DESC_LAST_FRAGMENT (1U << 28)
#define RTL8168_DESC_LENGTH_MASK 0x3FFFU

typedef struct __attribute__((packed)) {
    volatile uint32_t options;
    volatile uint32_t options2;
    volatile uint32_t address_low;
    volatile uint32_t address_high;
} rtl8168_descriptor_t;

typedef struct {
    volatile uint8_t *mmio;
    uint8_t irq;
    uint8_t tx_next;
    uint8_t rx_next;
    uint8_t phy_status;
    bool link_up;
    bool initialized;
} rtl8168_device_t;

static rtl8168_device_t rtl8168_device;
static rtl8168_descriptor_t rtl8168_tx_ring[RTL8168_TX_RING_COUNT]
    __attribute__((aligned(256)));
static rtl8168_descriptor_t rtl8168_rx_ring[RTL8168_RX_RING_COUNT]
    __attribute__((aligned(256)));
static uint8_t rtl8168_tx_buffers[RTL8168_TX_RING_COUNT]
                                   [RTL8168_MAX_FRAME_SIZE]
    __attribute__((aligned(16)));
static uint8_t rtl8168_rx_buffers[RTL8168_RX_RING_COUNT]
                                   [RTL8168_RX_BUFFER_SIZE]
    __attribute__((aligned(16)));
static volatile uint32_t rtl8168_tx_busy;
static volatile bool rtl8168_rx_pending;

static inline uint8_t rtl8168_read8(uint32_t offset) {
    return rtl8168_device.mmio[offset];
}

static inline uint16_t rtl8168_read16(uint32_t offset) {
    return *(volatile uint16_t *)(rtl8168_device.mmio + offset);
}

static inline void rtl8168_write8(uint32_t offset, uint8_t value) {
    rtl8168_device.mmio[offset] = value;
}

static inline void rtl8168_write16(uint32_t offset, uint16_t value) {
    *(volatile uint16_t *)(rtl8168_device.mmio + offset) = value;
}

static inline void rtl8168_write32(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(rtl8168_device.mmio + offset) = value;
}

static uint32_t rtl8168_dma_address(const void *address) {
    uintptr_t value = (uintptr_t)address;
    return (uint32_t)value;
}

static bool rtl8168_wait_reset(void) {
    for (uint32_t attempt = 0U; attempt < RTL8168_POLL_LIMIT; ++attempt) {
        if ((rtl8168_read8(RTL8168_CHIP_COMMAND) & RTL8168_CMD_RESET) == 0U)
            return true;
    }
    return false;
}

static bool rtl8168_refresh_link(void) {
    if (rtl8168_device.mmio == NULL) {
        rtl8168_device.phy_status = 0U;
        rtl8168_device.link_up = false;
        return false;
    }
    rtl8168_device.phy_status = rtl8168_read8(RTL8168_PHY_STATUS);
    rtl8168_device.link_up =
        (rtl8168_device.phy_status & RTL8168_PHY_LINK_STATUS) != 0U;
    return rtl8168_device.link_up;
}

static bool rtl8168_wait_for_link(void) {
    /* The copper PHY negotiates asynchronously after reset.  Give it a
     * bounded window before the first DHCP packet, while still allowing the
     * supervisor's later retry cycles to recover a cable/bridge that comes
     * up after boot. */
    for (uint32_t attempt = 0U; attempt < 100U; ++attempt) {
        if (rtl8168_refresh_link()) return true;
        pit_delay(10U);
    }
    return rtl8168_refresh_link();
}

static void rtl8168_arm_rx_descriptor(uint32_t index) {
    uint32_t ring_end = index + 1U == RTL8168_RX_RING_COUNT
        ? RTL8168_DESC_RING_END : 0U;
    rtl8168_rx_ring[index].options2 = 0U;
    __asm__ volatile("" ::: "memory");
    /* RTL8168 RX descriptors carry the writable buffer capacity in the low
     * 14 bits while OWN is set. A zero length makes the physical controller
     * reject incoming frames even though the ring address itself is valid. */
    rtl8168_rx_ring[index].options =
        RTL8168_DESC_OWN | ring_end |
        (RTL8168_RX_BUFFER_SIZE & RTL8168_DESC_LENGTH_MASK);
}

static void rtl8168_initialize_rings(void) {
    for (uint32_t index = 0U; index < RTL8168_TX_RING_COUNT; ++index) {
        rtl8168_tx_ring[index].options = index + 1U == RTL8168_TX_RING_COUNT
            ? RTL8168_DESC_RING_END : 0U;
        rtl8168_tx_ring[index].options2 = 0U;
        rtl8168_tx_ring[index].address_low =
            rtl8168_dma_address(rtl8168_tx_buffers[index]);
        rtl8168_tx_ring[index].address_high = 0U;
    }
    for (uint32_t index = 0U; index < RTL8168_RX_RING_COUNT; ++index) {
        rtl8168_rx_ring[index].address_low =
            rtl8168_dma_address(rtl8168_rx_buffers[index]);
        rtl8168_rx_ring[index].address_high = 0U;
        rtl8168_arm_rx_descriptor(index);
    }
    rtl8168_device.tx_next = 0U;
    rtl8168_device.rx_next = 0U;
}

static bool rtl8168_hardware_init(void) {
    rtl8168_write8(RTL8168_CHIP_COMMAND, RTL8168_CMD_RESET);
    if (!rtl8168_wait_reset()) {
        printf("RTL8168: reset timeout\n");
        return false;
    }

    rtl8168_initialize_rings();
    rtl8168_write16(RTL8168_INTERRUPT_MASK, 0U);
    rtl8168_write16(RTL8168_INTERRUPT_STATUS, 0xFFFFU);
    rtl8168_write32(RTL8168_TX_DESC_LOW,
                     rtl8168_dma_address(rtl8168_tx_ring));
    rtl8168_write32(RTL8168_TX_DESC_HIGH, 0U);
    rtl8168_write32(RTL8168_RX_DESC_LOW,
                     rtl8168_dma_address(rtl8168_rx_ring));
    rtl8168_write32(RTL8168_RX_DESC_HIGH, 0U);
    rtl8168_write16(RTL8168_RX_MAX_SIZE, RTL8168_RX_BUFFER_SIZE);
    rtl8168_write16(RTL8168_CPLUS_COMMAND, 0U);
    rtl8168_write32(RTL8168_RX_CONFIG,
                    RTL8168_RX_ACCEPT_BROADCAST |
                    RTL8168_RX_ACCEPT_MULTICAST |
                    RTL8168_RX_ACCEPT_PHYSICAL_MATCH |
                    RTL8168_RX_DMA_BURST | RTL8168_RX_FIFO_THRESHOLD);
    rtl8168_write32(RTL8168_TX_CONFIG, 7U << 8);
    rtl8168_write8(RTL8168_CONFIG_1,
                   (uint8_t)(rtl8168_read8(RTL8168_CONFIG_1) | 0x01U));
    bool link_up = rtl8168_wait_for_link();
    printf("RTL8168/8111G PHY status=0x%02X link=%s\n",
           rtl8168_device.phy_status, link_up ? "UP" : "DOWN");
    return true;
}

static void rtl8168_interrupt_handler(Registers *registers) {
    (void)registers;
    if (!rtl8168_device.initialized) return;
    uint16_t status = rtl8168_read16(RTL8168_INTERRUPT_STATUS);
    if (status == 0U || status == 0xFFFFU) return;
    rtl8168_write16(RTL8168_INTERRUPT_STATUS, status);
    if ((status & (RTL8168_INT_RX_OK | RTL8168_INT_RX_ERROR |
                   RTL8168_INT_RX_OVERFLOW | RTL8168_INT_RX_FIFO_OVERFLOW)) != 0U)
        __atomic_store_n(&rtl8168_rx_pending, true, __ATOMIC_RELEASE);
}

static void rtl8168_enable(void) {
    rtl8168_write8(RTL8168_CHIP_COMMAND,
                   RTL8168_CMD_RX_ENABLE | RTL8168_CMD_TX_ENABLE);
    rtl8168_write16(RTL8168_INTERRUPT_STATUS, 0xFFFFU);
    rtl8168_write16(RTL8168_INTERRUPT_MASK,
                    RTL8168_INT_RX_OK | RTL8168_INT_RX_ERROR |
                    RTL8168_INT_RX_OVERFLOW | RTL8168_INT_RX_FIFO_OVERFLOW |
                    RTL8168_INT_LINK_CHANGE);
}

bool rtl8168_send_packet(const uint8_t *packet, size_t length) {
    if (!rtl8168_device.initialized || netdev_outputs_fenced() || !packet ||
        length < 14U || length > RTL8168_MAX_FRAME_SIZE ||
        __sync_lock_test_and_set(&rtl8168_tx_busy, 1U) != 0U) return false;

    if (!rtl8168_refresh_link()) {
        __sync_lock_release(&rtl8168_tx_busy);
        return false;
    }

    uint32_t index = rtl8168_device.tx_next;
    rtl8168_descriptor_t *descriptor = &rtl8168_tx_ring[index];
    bool available = false;
    for (uint32_t attempt = 0U; attempt < RTL8168_POLL_LIMIT; ++attempt) {
        if ((descriptor->options & RTL8168_DESC_OWN) == 0U) {
            available = true;
            break;
        }
    }
    if (!available) {
        __sync_lock_release(&rtl8168_tx_busy);
        return false;
    }

    memcpy(rtl8168_tx_buffers[index], packet, length);
    uint32_t ring_end = index + 1U == RTL8168_TX_RING_COUNT
        ? RTL8168_DESC_RING_END : 0U;
    descriptor->options2 = 0U;
    descriptor->address_low = rtl8168_dma_address(rtl8168_tx_buffers[index]);
    descriptor->address_high = 0U;
    __asm__ volatile("" ::: "memory");
    descriptor->options = RTL8168_DESC_OWN | ring_end |
                          RTL8168_DESC_FIRST_FRAGMENT |
                          RTL8168_DESC_LAST_FRAGMENT |
                          ((uint32_t)length & RTL8168_DESC_LENGTH_MASK);
    rtl8168_device.tx_next = (uint8_t)((index + 1U) % RTL8168_TX_RING_COUNT);
    rtl8168_write8(RTL8168_TX_POLL, 0x40U);

    bool completed = false;
    for (uint32_t attempt = 0U; attempt < RTL8168_POLL_LIMIT; ++attempt) {
        if ((descriptor->options & RTL8168_DESC_OWN) == 0U) {
            completed = true;
            break;
        }
    }
    __sync_lock_release(&rtl8168_tx_busy);
    return completed;
}

void rtl8168_poll_rx(void) {
    if (!rtl8168_device.initialized) return;
    bool pending = __atomic_exchange_n(&rtl8168_rx_pending, false,
                                       __ATOMIC_ACQ_REL);
    (void)pending;
    for (uint32_t processed = 0U; processed < RTL8168_RX_RING_COUNT;
         ++processed) {
        uint32_t index = rtl8168_device.rx_next;
        rtl8168_descriptor_t *descriptor = &rtl8168_rx_ring[index];
        uint32_t options = descriptor->options;
        if ((options & RTL8168_DESC_OWN) != 0U) break;

        uint32_t length = options & RTL8168_DESC_LENGTH_MASK;
        bool complete = (options & (RTL8168_DESC_FIRST_FRAGMENT |
                                    RTL8168_DESC_LAST_FRAGMENT)) ==
                        (RTL8168_DESC_FIRST_FRAGMENT |
                         RTL8168_DESC_LAST_FRAGMENT);
        if (complete && length >= 18U && length <= RTL8168_RX_BUFFER_SIZE) {
            /* The controller reports the four-byte Ethernet FCS in length. */
            netdev_deliver_rx(rtl8168_rx_buffers[index],
                              (uint16_t)(length - 4U));
        }
        rtl8168_arm_rx_descriptor(index);
        rtl8168_device.rx_next =
            (uint8_t)((index + 1U) % RTL8168_RX_RING_COUNT);
    }
}

void rtl8168_get_mac_address(uint8_t mac[6]) {
    if (!mac) return;
    if (!rtl8168_device.initialized) {
        memset(mac, 0, 6U);
        return;
    }
    for (uint32_t index = 0U; index < 6U; ++index)
        mac[index] = rtl8168_read8(RTL8168_MAC0 + index);
}

void rtl8168_fence_outputs(void) {
    if (!rtl8168_device.initialized) return;
    rtl8168_write16(RTL8168_INTERRUPT_MASK, 0U);
    uint8_t command = rtl8168_read8(RTL8168_CHIP_COMMAND);
    rtl8168_write8(RTL8168_CHIP_COMMAND,
                   (uint8_t)(command & (uint8_t)~(RTL8168_CMD_RX_ENABLE |
                                                  RTL8168_CMD_TX_ENABLE)));
}

bool rtl8168_outputs_fenced(void) {
    if (!rtl8168_device.initialized) return true;
    uint8_t command = rtl8168_read8(RTL8168_CHIP_COMMAND);
    return (rtl8168_read16(RTL8168_INTERRUPT_MASK) == 0U) &&
           (command & (RTL8168_CMD_RX_ENABLE | RTL8168_CMD_TX_ENABLE)) == 0U;
}

static bool rtl8168_find_mmio_bar(pci_device_t *pci_device,
                                  uint32_t *base) {
    uint32_t order[6] = {2U, 0U, 1U, 3U, 4U, 5U};
    for (uint32_t order_index = 0U; order_index < 6U; ++order_index) {
        uint32_t index = order[order_index];
        uint32_t raw = pci_read_bar(pci_device, (uint8_t)index);
        if (raw == 0U || raw == 0xFFFFFFFFU || (raw & 1U) != 0U) continue;
        if ((raw & 0x6U) == 0x4U) {
            if (index >= 5U || pci_read_bar(pci_device, (uint8_t)(index + 1U)) != 0U)
                continue;
        }
        raw &= ~0xFU;
        if (raw != 0U) {
            *base = raw;
            return true;
        }
    }
    return false;
}

static int rtl8168_probe(pci_device_t *pci_device) {
    if (!pci_device || pci_device->vendor_id != RTL8168_VENDOR_ID ||
        pci_device->device_id != RTL8168_DEVICE_ID) return -1;

    uint32_t mmio_base = 0U;
    if (!rtl8168_find_mmio_bar(pci_device, &mmio_base)) {
        printf("RTL8168: no usable 32-bit MMIO BAR\n");
        return -1;
    }
    uint8_t irq = pci_configure_irq(pci_device);
    if (!pci_irq_is_valid(irq)) {
        printf("RTL8168: invalid legacy IRQ %u\n", irq);
        return -1;
    }
    volatile uint8_t *mmio = (volatile uint8_t *)map_mmio_region(
        mmio_base, RTL8168_MMIO_SIZE);
    if (!mmio) {
        printf("RTL8168: unable to map MMIO BAR 0x%08X\n", mmio_base);
        return -1;
    }

    rtl8168_device.mmio = mmio;
    rtl8168_device.irq = irq;
    pci_enable_device(pci_device);
    if (!rtl8168_hardware_init()) {
        rtl8168_device.mmio = NULL;
        return -1;
    }
    pci_set_bus_master(pci_device->bus, pci_device->slot,
                       pci_device->function, 1U);
    if (register_interrupt_handler(irq, rtl8168_interrupt_handler) != 0) {
        pci_set_bus_master(pci_device->bus, pci_device->slot,
                           pci_device->function, 0U);
        rtl8168_device.mmio = NULL;
        return -1;
    }
    rtl8168_device.initialized = true;
    rtl8168_enable();

    uint8_t mac[6];
    rtl8168_get_mac_address(mac);
    printf("RTL8168/8111G MAC: %02X:%02X:%02X:%02X:%02X:%02X, MMIO: 0x%08X, IRQ: %u\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mmio_base, irq);
    return 0;
}

void rtl8168_detect(void) {
    printf("Detecting Realtek RTL8168/8111G network card...\n");
    pci_register_driver_named(RTL8168_VENDOR_ID, RTL8168_DEVICE_ID,
                              "Realtek RTL8168/8111G", rtl8168_probe);
}

int rtl8168_is_initialized(void) {
    return rtl8168_device.initialized;
}

bool rtl8168_is_link_up(void) {
    return rtl8168_device.initialized && rtl8168_refresh_link();
}
