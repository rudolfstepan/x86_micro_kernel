#ifndef E1000_H
#define E1000_H

#include <stdint.h>
#include <stddef.h>

// Define constants for PCI configuration space and E1000 registers
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define E1000_VENDOR_ID    0x8086
#define E1000_DEVICE_ID    0x100E

// E1000 Register Offsets
#define E1000_REG_CTRL     0x0000
#define E1000_REG_STATUS   0x0008
#define E1000_REG_TCTL     0x0400
#define E1000_REG_RCTL     0x0100
#define E1000_REG_TIPG     0x0410
#define E1000_REG_TDBAL    0x3800
#define E1000_REG_TDBAH    0x3804
#define E1000_REG_TDLEN    0x3808
#define E1000_REG_TDH      0x3810
#define E1000_REG_TDT      0x3818
#define E1000_REG_RDBAL    0x2800
#define E1000_REG_RDBAH    0x2804
#define E1000_REG_RDLEN    0x2808
#define E1000_REG_RDH      0x2810
#define E1000_REG_RDT      0x2818
#define E1000_REG_IMS      0x00D0

#define	E1000_TXD_CMD_EOP   0x01000000 /* End of Packet */
#define	E1000_TXD_CMD_IFCS  0x02000000 /* Insert FCS (Ethernet CRC) */
#define	E1000_TXD_CMD_IC   0x04000000 /* Insert Checksum */
#define	E1000_TXD_CMD_RS   0x08000000 /* Report Status */
#define E1000_TXD_STAT_DD  0x00000001 /* Descriptor Done */
#define E1000_RCTL_UPE   0x00000008 /* unicast promiscuous enable */
#define E1000_RCTL_MPE   0x00000010 /* multicast promiscuous enab */



// PIC-Konstanten
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1


// MMIO base address of the E1000 device (to be set up via PCI configuration)
#define E1000_MMIO_BASE 0xF0000000

// Register offsets (consult the E1000 datasheet)
#define E1000_CTRL      0x00000  // Device Control Register
#define E1000_STATUS    0x00008  // Device Status Register
#define E1000_RCTL      0x00100  // Receive Control Register
#define E1000_TCTL      0x00400  // Transmit Control Register
#define E1000_ICR       0x000C0  // Interrupt Cause Read
#define E1000_IMS       0x000D0  // Interrupt Mask Set/Read
#define E1000_ICS       0x000C8  // Interrupt Cause Set
#define E1000_RDBAL     0x02800  // Receive Descriptor Base Low
#define E1000_RDBAH     0x02804  // Receive Descriptor Base High
#define E1000_RDLEN     0x02808  // Receive Descriptor Length
#define E1000_TDBAL     0x03800  // Transmit Descriptor Base Low
#define E1000_TDBAH     0x03804  // Transmit Descriptor Base High
#define E1000_TDLEN     0x03808  // Transmit Descriptor Length

// RCTL Register Bits
#define E1000_RCTL_EN       0x00000002  // Receiver Enable
#define E1000_RCTL_LBM_MAC  0x00000040  // Loopback Mode

// TCTL Register Bits
#define E1000_TCTL_EN       0x00000002  // Transmitter Enable

// IMS Register Bits
#define E1000_IMS_RXT0      0x00000080  // Receive Timer Interrupt

#define E1000_CTRL_RST 0x04000000 // Device Reset
#define E1000_CTRL_PHY_RST 0x80000000 // PHY Reset

#define E1000_NUM_RX_DESC 32
#define E1000_NUM_TX_DESC 8

#define REG_RCTRL       0x0100

#define REG_CTRL        0x0000
#define REG_STATUS      0x0008
#define REG_EEPROM      0x0014
#define REG_CTRL_EXT    0x0018
#define REG_IMASK       0x00D0
#define REG_RCTRL       0x0100
#define REG_RXDESCLO    0x2800
#define REG_RXDESCHI    0x2804
#define REG_RXDESCLEN   0x2808
#define REG_RXDESCHEAD  0x2810
#define REG_RXDESCTAIL  0x2818

#define REG_TCTRL       0x0400
#define REG_TXDESCLO    0x3800
#define REG_TXDESCHI    0x3804
#define REG_TXDESCLEN   0x3808
#define REG_TXDESCHEAD  0x3810
#define REG_TXDESCTAIL  0x3818


#define REG_RDTR         0x2820 // RX Delay Timer Register
#define REG_RXDCTL       0x2828 // RX Descriptor Control
#define REG_RADV         0x282C // RX Int. Absolute Delay Timer
#define REG_RSRPD        0x2C00 // RX Small Packet Detect Interrupt


#define REG_RDTR         0x2820 // RX Delay Timer Register
#define REG_RXDCTL       0x2828 // RX Descriptor Control
#define REG_RADV         0x282C // RX Int. Absolute Delay Timer
#define REG_RSRPD        0x2C00 // RX Small Packet Detect Interrupt



#define REG_TIPG         0x0410      // Transmit Inter Packet Gap
#define ECTRL_SLU        0x40        //set link up


#define RCTL_EN                         (1 << 1)    // Receiver Enable
#define RCTL_SBP                        (1 << 2)    // Store Bad Packets
#define RCTL_UPE                        (1 << 3)    // Unicast Promiscuous Enabled
#define RCTL_MPE                        (1 << 4)    // Multicast Promiscuous Enabled
#define RCTL_LPE                        (1 << 5)    // Long Packet Reception Enable
#define RCTL_LBM_NONE                   (0 << 6)    // No Loopback
#define RCTL_LBM_PHY                    (3 << 6)    // PHY or external SerDesc loopback
#define RTCL_RDMTS_HALF                 (0 << 8)    // Free Buffer Threshold is 1/2 of RDLEN
#define RTCL_RDMTS_QUARTER              (1 << 8)    // Free Buffer Threshold is 1/4 of RDLEN
#define RTCL_RDMTS_EIGHTH               (2 << 8)    // Free Buffer Threshold is 1/8 of RDLEN
#define RCTL_MO_36                      (0 << 12)   // Multicast Offset - bits 47:36
#define RCTL_MO_35                      (1 << 12)   // Multicast Offset - bits 46:35
#define RCTL_MO_34                      (2 << 12)   // Multicast Offset - bits 45:34
#define RCTL_MO_32                      (3 << 12)   // Multicast Offset - bits 43:32
#define RCTL_BAM                        (1 << 15)   // Broadcast Accept Mode
#define RCTL_VFE                        (1 << 18)   // VLAN Filter Enable
#define RCTL_CFIEN                      (1 << 19)   // Canonical Form Indicator Enable
#define RCTL_CFI                        (1 << 20)   // Canonical Form Indicator Bit Value
#define RCTL_DPF                        (1 << 22)   // Discard Pause Frames
#define RCTL_PMCF                       (1 << 23)   // Pass MAC Control Frames
#define RCTL_SECRC                      (1 << 26)   // Strip Ethernet CRC

// Buffer Sizes
#define RCTL_BSIZE_256                  (3 << 16)
#define RCTL_BSIZE_512                  (2 << 16)
#define RCTL_BSIZE_1024                 (1 << 16)
#define RCTL_BSIZE_2048                 (0 << 16)
#define RCTL_BSIZE_4096                 ((3 << 16) | (1 << 25))
#define RCTL_BSIZE_8192                 ((2 << 16) | (1 << 25))
#define RCTL_BSIZE_16384                ((1 << 16) | (1 << 25))


// Transmit Command

#define CMD_EOP                         (1 << 0)    // End of Packet
#define CMD_IFCS                        (1 << 1)    // Insert FCS
#define CMD_IC                          (1 << 2)    // Insert Checksum
#define CMD_RS                          (1 << 3)    // Report Status
#define CMD_RPS                         (1 << 4)    // Report Packet Sent
#define CMD_VLE                         (1 << 6)    // VLAN Packet Enable
#define CMD_IDE                         (1 << 7)    // Interrupt Delay Enable


// TCTL Register

#define TCTL_EN                         (1 << 1)    // Transmit Enable
#define TCTL_PSP                        (1 << 3)    // Pad Short Packets
#define TCTL_CT_SHIFT                   4           // Collision Threshold
#define TCTL_COLD_SHIFT                 12          // Collision Distance
#define TCTL_SWXOFF                     (1 << 22)   // Software XOFF Transmission
#define TCTL_RTLC                       (1 << 24)   // Re-transmit on Late Collision

#define TSTA_DD                         (1 << 0)    // Descriptor Done
#define TSTA_EC                         (1 << 1)    // Excess Collisions
#define TSTA_LC                         (1 << 2)    // Late Collision
#define LSTA_TU                         (1 << 3)    // Transmit Underrun

// Define the size of the transmit and receive rings
struct e1000_tx_desc {
    volatile uint64_t buffer_addr; // Address of the data buffer
    volatile uint16_t length;      // Length of the data buffer
    volatile uint8_t  cso;         // Checksum offset
    volatile uint8_t  cmd;         // Command field (RS, IC, EOP, etc.)
    volatile uint8_t  status;      // Descriptor status (DD bit)
    volatile uint8_t  css;         // Checksum start
    volatile uint16_t special;     // Special field
} __attribute__((packed));

struct e1000_rx_desc {
    volatile uint64_t buffer_addr; // Address of the data buffer
    volatile uint16_t length;      // Length of the received data
    volatile uint16_t checksum;    // Packet checksum
    volatile uint8_t  status;      // Descriptor status (DD, EOP bits)
    volatile uint8_t  errors;      // Errors during reception
    volatile uint16_t special;     // Special field
} __attribute__((packed));

void e1000_detect();
void e1000_send_test_packet();

#endif // E1000_H