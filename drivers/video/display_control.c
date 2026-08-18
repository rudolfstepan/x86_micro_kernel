#include "display_control.h"

#include <stdbool.h>
#include <stdint.h>

#include "drivers/bus/pci.h"
#include "drivers/char/io.h"
#include "drivers/video/framebuffer.h"
#include "include/lib/spinlock.h"
#include "lib/libc/stdio.h"

#define DISPI_INDEX 0x01CEU
#define DISPI_DATA  0x01CFU
#define DISPI_ID 0x0000U
#define DISPI_XRES 0x0001U
#define DISPI_YRES 0x0002U
#define DISPI_BPP 0x0003U
#define DISPI_ENABLE 0x0004U
#define DISPI_VIRT_WIDTH 0x0006U
#define DISPI_VIRT_HEIGHT 0x0007U
#define DISPI_X_OFFSET 0x0008U
#define DISPI_Y_OFFSET 0x0009U
#define DISPI_VIDEO_MEMORY_64K 0x000AU
#define DISPI_ENABLED 0x0001U
#define DISPI_LFB_ENABLED 0x0040U

/* VMware SVGA II (legacy BIOS Workstation device).  The device exposes an
 * indexed register pair in BAR0 and its linear framebuffer in BAR1. */
#define VMWARE_VENDOR 0x15ADU
#define VMWARE_DEVICE 0x0405U
#define SVGA_REG_ID 0U
#define SVGA_REG_ENABLE 1U
#define SVGA_REG_WIDTH 2U
#define SVGA_REG_HEIGHT 3U
#define SVGA_REG_MAX_WIDTH 4U
#define SVGA_REG_MAX_HEIGHT 5U
#define SVGA_REG_BITS_PER_PIXEL 7U
#define SVGA_REG_BYTES_PER_LINE 12U
#define SVGA_REG_FB_START 13U
#define SVGA_REG_FB_OFFSET 14U
#define SVGA_REG_FB_SIZE 16U
#define SVGA_REG_MEM_START 18U
#define SVGA_REG_MEM_SIZE 19U
#define SVGA_REG_CONFIG_DONE 20U
#define SVGA_REG_SYNC 21U
#define SVGA_REG_MEM_REGS 30U
#define SVGA_ID_2 0x90000002U
#define SVGA_ID_1 0x90000001U
#define SVGA_ENABLE 1U
#define SVGA_FIFO_MIN 0U
#define SVGA_FIFO_MAX 1U
#define SVGA_FIFO_NEXT_CMD 2U
#define SVGA_FIFO_STOP 3U
#define SVGA_CMD_UPDATE 1U

static volatile bool activation_busy;
static volatile uint32_t *vmware_fifo;
static uint16_t vmware_index_port;
static uint16_t vmware_value_port;

static void svga_write(uint16_t index_port, uint16_t value_port,
                       uint32_t index, uint32_t value);

static bool vmware_fifo_write(uint32_t value) {
    if (!vmware_fifo) return false;
    uint32_t minimum = vmware_fifo[SVGA_FIFO_MIN];
    uint32_t maximum = vmware_fifo[SVGA_FIFO_MAX];
    uint32_t next = vmware_fifo[SVGA_FIFO_NEXT_CMD];
    uint32_t stop = vmware_fifo[SVGA_FIFO_STOP];
    if (minimum < 4U * sizeof(uint32_t) || maximum <= minimum ||
        (minimum & 3U) != 0U || (maximum & 3U) != 0U ||
        next < minimum || next >= maximum || stop < minimum || stop >= maximum)
        return false;
    uint32_t following = next + sizeof(uint32_t);
    if (following == maximum) following = minimum;
    if (following == stop) return false;
    vmware_fifo[next / sizeof(uint32_t)] = value;
    vmware_fifo[SVGA_FIFO_NEXT_CMD] = following;
    return true;
}

void display_control_present_rect(uint32_t x, uint32_t y,
                                  uint32_t width, uint32_t height) {
    if (!vmware_fifo || width == 0U || height == 0U) return;
    if (!vmware_fifo_write(SVGA_CMD_UPDATE) ||
        !vmware_fifo_write(x) || !vmware_fifo_write(y) ||
        !vmware_fifo_write(width) || !vmware_fifo_write(height)) return;
    svga_write(vmware_index_port, vmware_value_port, SVGA_REG_SYNC, 1U);
}

static uint16_t dispi_read(uint16_t index) {
    outw(DISPI_INDEX, index);
    return inw(DISPI_DATA);
}

static void dispi_write(uint16_t index, uint16_t value) {
    outw(DISPI_INDEX, index);
    outw(DISPI_DATA, value);
}

static uint32_t svga_read(uint16_t index_port, uint16_t value_port,
                          uint32_t index) {
    outl(index_port, index);
    return inl(value_port);
}

static void svga_write(uint16_t index_port, uint16_t value_port,
                       uint32_t index, uint32_t value) {
    outl(index_port, index);
    outl(value_port, value);
}

static pci_device_t *find_qemu_vga(uint32_t *lfb_out) {
    for (size_t index = 0; index < pci_device_count; ++index) {
        pci_device_t *device = &pci_devices[index];
        if (device->vendor_id != 0x1234U || device->device_id != 0x1111U ||
            device->class_code != 0x03U || device->subclass_code != 0x00U)
            continue;
        uint32_t bar = device->bar[0];
        /* Memory BAR type bits 1..2 must describe a 32-bit BAR; bit 3 may
         * legally mark the region prefetchable. */
        if ((bar & 0x06U) != 0U || (bar & 0xFFFFFFF0U) == 0U) continue;
        *lfb_out = bar & 0xFFFFF000U;
        return device;
    }
    return NULL;
}

static int activate_vmware(pci_device_t *device) {
    uint32_t index_bar = device->bar[0];
    uint32_t framebuffer_bar = device->bar[1];
    uint32_t fifo_bar = device->bar[2];
    if ((index_bar & 1U) == 0U || (framebuffer_bar & 0x06U) != 0U ||
        (fifo_bar & 0x06U) != 0U ||
        (index_bar & 0xFFFFFFFCU) == 0U ||
        (framebuffer_bar & 0xFFFFFFF0U) == 0U ||
        (fifo_bar & 0xFFFFFFF0U) == 0U) return -19;
    uint16_t index_port = (uint16_t)(index_bar & 0xFFFCU);
    uint16_t value_port = (uint16_t)(index_port + 1U);
    /* SVGA-II negotiates the register protocol by writing the requested
     * version first; a read of the reset value is not a capability probe. */
    svga_write(index_port, value_port, SVGA_REG_ID, SVGA_ID_2);
    uint32_t id = svga_read(index_port, value_port, SVGA_REG_ID);
    if (id != SVGA_ID_2) {
        svga_write(index_port, value_port, SVGA_REG_ID, SVGA_ID_1);
        id = svga_read(index_port, value_port, SVGA_REG_ID);
    }
    if (id != SVGA_ID_1 && id != SVGA_ID_2) return -19;
    uint32_t max_width = svga_read(index_port, value_port, SVGA_REG_MAX_WIDTH);
    uint32_t max_height = svga_read(index_port, value_port, SVGA_REG_MAX_HEIGHT);
    uint32_t width = max_width >= 1024U ? 1024U : 800U;
    uint32_t height = max_height >= 768U ? 768U : 600U;
    uint64_t required = (uint64_t)width * height * 4U;
    uint32_t fb_size = svga_read(index_port, value_port, SVGA_REG_FB_SIZE);
    if (max_width < 800U || max_height < 600U || required > fb_size) return -19;
    pci_enable_device(device);
    svga_write(index_port, value_port, SVGA_REG_ENABLE, 0U);
    svga_write(index_port, value_port, SVGA_REG_WIDTH, width);
    svga_write(index_port, value_port, SVGA_REG_HEIGHT, height);
    svga_write(index_port, value_port, SVGA_REG_BITS_PER_PIXEL, 32U);
    svga_write(index_port, value_port, SVGA_REG_ENABLE, SVGA_ENABLE);
    if (svga_read(index_port, value_port, SVGA_REG_WIDTH) != width ||
        svga_read(index_port, value_port, SVGA_REG_HEIGHT) != height ||
        svga_read(index_port, value_port, SVGA_REG_BITS_PER_PIXEL) != 32U ||
        (svga_read(index_port, value_port, SVGA_REG_ENABLE) & SVGA_ENABLE) == 0U)
        return -19;
    uint32_t pitch = svga_read(index_port, value_port, SVGA_REG_BYTES_PER_LINE);
    uint32_t framebuffer_start =
        svga_read(index_port, value_port, SVGA_REG_FB_START);
    uint32_t framebuffer_offset =
        svga_read(index_port, value_port, SVGA_REG_FB_OFFSET);
    uint64_t visible_bytes = (uint64_t)pitch * height;
    uint64_t framebuffer_address =
        (uint64_t)framebuffer_start + framebuffer_offset;
    if (pitch < width * 4U || framebuffer_start == 0U ||
        framebuffer_address > UINT32_MAX ||
        framebuffer_offset > fb_size ||
        visible_bytes > (uint64_t)fb_size - framebuffer_offset)
        return -19;
    uint32_t fifo_start = svga_read(index_port, value_port, SVGA_REG_MEM_START);
    uint32_t fifo_size = svga_read(index_port, value_port, SVGA_REG_MEM_SIZE);
    uint32_t fifo_registers =
        svga_read(index_port, value_port, SVGA_REG_MEM_REGS);
    uint32_t fifo_minimum = fifo_registers * sizeof(uint32_t);
    if (fifo_start == 0U) fifo_start = fifo_bar & 0xFFFFFFF0U;
    if (fifo_size < 4096U || fifo_registers < 4U ||
        fifo_registers > fifo_size / sizeof(uint32_t) ||
        fifo_minimum > fifo_size - 5U * sizeof(uint32_t)) return -19;
    volatile uint32_t *fifo = map_mmio_region(fifo_start, fifo_size);
    if (!fifo) return -19;
    fifo[SVGA_FIFO_MIN] = fifo_minimum;
    fifo[SVGA_FIFO_MAX] = fifo_size;
    fifo[SVGA_FIFO_NEXT_CMD] = fifo_minimum;
    fifo[SVGA_FIFO_STOP] = fifo_minimum;
    svga_write(index_port, value_port, SVGA_REG_CONFIG_DONE, 1U);
    multiboot_framebuffer_info_t info = {
        .framebuffer_addr = framebuffer_address,
        .framebuffer_pitch = pitch, .framebuffer_width = width,
        .framebuffer_height = height, .framebuffer_bpp = 32U,
        .framebuffer_type = 1U, .red_field_position = 16U,
        .red_mask_size = 8U, .green_field_position = 8U,
        .green_mask_size = 8U, .blue_field_position = 0U,
        .blue_mask_size = 8U
    };
    framebuffer_init(&info);
    if (!framebuffer_available()) return -19;
    vmware_fifo = fifo;
    vmware_index_port = index_port;
    vmware_value_port = value_port;
    display_control_present_rect(0U, 0U, width, height);
    return 0;
}

int display_control_activate(void) {
    if (framebuffer_available()) return 0;
    uint32_t old_flags;
    __asm__ __volatile__("pushf\n pop %0\n cli" : "=r"(old_flags) :: "memory");
    if (activation_busy) {
        __asm__ __volatile__("push %0\n popf" :: "r"(old_flags) : "memory");
        return -16;
    }
    activation_busy = true;
    __asm__ __volatile__("push %0\n popf" :: "r"(old_flags) : "memory");

    int result = -19;
    uint32_t lfb_address = 0;
    pci_device_t *device = find_qemu_vga(&lfb_address);
    if (!device) {
        for (size_t index = 0; index < pci_device_count; ++index) {
            pci_device_t *candidate = &pci_devices[index];
            if (candidate->vendor_id == VMWARE_VENDOR &&
                candidate->device_id == VMWARE_DEVICE &&
                candidate->class_code == 0x03U) {
                result = activate_vmware(candidate);
                goto activation_done;
            }
        }
    }
    uint16_t id = dispi_read(DISPI_ID);
    uint16_t memory_64k = dispi_read(DISPI_VIDEO_MEMORY_64K);
    bool supported_id = id >= 0xB0C0U && id <= 0xB0C5U;
    uint32_t width = memory_64k >= 48U ? 1024U : 800U;
    uint32_t height = memory_64k >= 48U ? 768U : 600U;
    uint64_t required = (uint64_t)width * height * 4U;
    bool valid = device != NULL && supported_id && memory_64k != 0U &&
                 required <= (uint64_t)memory_64k * 65536U &&
                 lfb_address != 0U;
    if (valid) {
        pci_enable_device(device);
        dispi_write(DISPI_ENABLE, 0U);
        dispi_write(DISPI_XRES, (uint16_t)width);
        dispi_write(DISPI_YRES, (uint16_t)height);
        dispi_write(DISPI_BPP, 32U);
        dispi_write(DISPI_VIRT_WIDTH, (uint16_t)width);
        dispi_write(DISPI_VIRT_HEIGHT, (uint16_t)height);
        dispi_write(DISPI_X_OFFSET, 0U);
        dispi_write(DISPI_Y_OFFSET, 0U);
        dispi_write(DISPI_ENABLE, DISPI_ENABLED | DISPI_LFB_ENABLED);
        valid = dispi_read(DISPI_XRES) == width &&
                dispi_read(DISPI_YRES) == height &&
                dispi_read(DISPI_BPP) == 32U &&
                (dispi_read(DISPI_ENABLE) &
                 (DISPI_ENABLED | DISPI_LFB_ENABLED)) ==
                    (DISPI_ENABLED | DISPI_LFB_ENABLED);
        if (valid) {
            multiboot_framebuffer_info_t info = {
                .framebuffer_addr = lfb_address,
                .framebuffer_pitch = width * 4U,
                .framebuffer_width = width,
                .framebuffer_height = height,
                .framebuffer_bpp = 32U,
                .framebuffer_type = 1U,
                .red_field_position = 16U, .red_mask_size = 8U,
                .green_field_position = 8U, .green_mask_size = 8U,
                .blue_field_position = 0U, .blue_mask_size = 8U
            };
            framebuffer_init(&info);
            if (framebuffer_available()) result = 0;
        }
        if (result != 0) dispi_write(DISPI_ENABLE, 0U);
    }
activation_done:
    if (result != 0) printf("DISPLAY_CONTROL: native graphics unavailable\n");
    __asm__ __volatile__("cli" ::: "memory");
    activation_busy = false;
    __asm__ __volatile__("push %0\n popf" :: "r"(old_flags) : "memory");
    return result;
}
