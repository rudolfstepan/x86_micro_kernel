#include "display_control.h"

#include <stdbool.h>
#include <stdint.h>

#include "drivers/bus/pci.h"
#include "drivers/char/io.h"
#include "drivers/video/framebuffer.h"
#include "arch/x86/boot/vbe_runtime.h"
#include "arch/x86/mm/paging.h"
#include "include/kernel/device_domain.h"
#include "include/lib/spinlock.h"
#include "kernel/sched/mutex.h"
#include "kernel/sched/scheduler.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"

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
#define VMWARE_DEVICE_SVGA2 0x0405U
#define VMWARE_DEVICE_SVGA 0x0710U
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
#define SVGA_REG_CAPABILITIES 17U
#define SVGA_REG_MEM_START 18U
#define SVGA_REG_MEM_SIZE 19U
#define SVGA_REG_CONFIG_DONE 20U
#define SVGA_REG_SYNC 21U
#define SVGA_REG_BUSY 22U
#define SVGA_REG_MEM_REGS 30U
#define SVGA_ID_2 0x90000002U
#define SVGA_ID_1 0x90000001U
#define SVGA_ENABLE 1U
#define SVGA_FIFO_MIN 0U
#define SVGA_FIFO_MAX 1U
#define SVGA_FIFO_NEXT_CMD 2U
#define SVGA_FIFO_STOP 3U
/* VMware SVGA-II FIFO synchronization register. It is available only when
 * FIFO_MIN reserves all 291 register words reported by this exact device. */
#define SVGA_FIFO_BUSY 290U
#define SVGA_CMD_UPDATE 1U
#define SVGA_CMD_RECT_FILL 2U
#define SVGA_CMD_RECT_COPY 3U
#define SVGA_CAP_RECT_FILL (1U << 0U)
#define SVGA_CAP_RECT_COPY (1U << 1U)

/* NVIDIA GK208 (GeForce GT 635, PCI 10de:1280).  Ring 0 validates only the
 * immutable PCI/BAR geometry needed by the VBE fallback.  Register probing is
 * performed through the read-only device-domain aperture in Ring 3. */
#define NVIDIA_VENDOR 0x10DEU
#define NVIDIA_GK208_DEVICE 0x1280U
#define NVIDIA_REQUIRED_BAR_BYTES 0x00400104U
#define NVIDIA_BAR_MAX_BYTES (64U * 1024U * 1024U)

typedef enum {
    DISPLAY_BACKEND_NONE = 0,
    DISPLAY_BACKEND_QEMU,
    DISPLAY_BACKEND_VMWARE,
    DISPLAY_BACKEND_VBE
} display_backend_t;

static volatile bool activation_busy;
static bool qemu_prepared;
static bool vmware_prepared;
static bool vmware_supervised;
static bool nvidia_prepared;
static bool vbe_prepared;
static vbe_runtime_info_t vbe_runtime_info;
static uint32_t vbe_reject_reason;
static display_backend_t active_backend;
/* Unconfirmed device disable is not an inactive, reusable display. */
static display_backend_t mode_fault_backend;
static uint16_t mode_fault_index, mode_fault_value;
static volatile uint32_t *vmware_fifo;
static uint16_t vmware_index_port;
static uint16_t vmware_value_port;
static uint32_t vmware_capabilities;
static uint32_t vmware_width;
static uint32_t vmware_height;
static bool vmware_rect_copy_reported;
static kernel_mutex_t display_state_mutex = KERNEL_MUTEX_INIT;
static spinlock_t vmware_fifo_lock = SPINLOCK_INIT;

#define DISPLAY_STATE_TIMEOUT_MS 1000U

static void svga_write(uint16_t index_port, uint16_t value_port,
                       uint32_t index, uint32_t value);

static bool vmware_fifo_write_batch(const uint32_t *values, uint32_t count) {
    if (!vmware_fifo || values == NULL || count == 0U ||
        count > DISPLAY_CONTROL_PRESENT_CAPACITY * 5U) return false;
    uint32_t minimum = vmware_fifo[SVGA_FIFO_MIN];
    uint32_t maximum = vmware_fifo[SVGA_FIFO_MAX];
    uint32_t next = vmware_fifo[SVGA_FIFO_NEXT_CMD];
    uint32_t stop = vmware_fifo[SVGA_FIFO_STOP];
    if (minimum < 4U * sizeof(uint32_t) || maximum <= minimum ||
        (minimum & 3U) != 0U || (maximum & 3U) != 0U ||
        next < minimum || next >= maximum || stop < minimum || stop >= maximum)
        return false;
    uint32_t free_bytes = next >= stop
        ? (maximum - next) + (stop - minimum) : stop - next;
    if (free_bytes < sizeof(uint32_t) ||
        count > (free_bytes - sizeof(uint32_t)) / sizeof(uint32_t))
        return false;
    for (uint32_t index = 0U; index < count; ++index) {
        vmware_fifo[next / sizeof(uint32_t)] = values[index];
        next += sizeof(uint32_t);
        if (next == maximum) next = minimum;
    }
    vmware_fifo[SVGA_FIFO_NEXT_CMD] = next;
    return true;
}

/* Call only with vmware_fifo_lock held after committing FIFO commands. VMware
 * SVGA-II specifies FIFO_BUSY as the low-cost asynchronous wakeup latch: when
 * the host is already consuming the FIFO, newly committed commands are
 * guaranteed to be observed and another synchronous I/O doorbell is harmful
 * for cursor cadence. Old or minimal FIFO layouts retain one SYNC per batch. */
static bool vmware_fifo_doorbell_needed(void) {
    if (!vmware_fifo) return false;
    uint32_t minimum = vmware_fifo[SVGA_FIFO_MIN];
    if (minimum < (SVGA_FIFO_BUSY + 1U) * sizeof(uint32_t)) return true;
    __asm__ __volatile__("" ::: "memory");
    if (vmware_fifo[SVGA_FIFO_BUSY] != 0U) return false;
    vmware_fifo[SVGA_FIFO_BUSY] = 1U;
    return true;
}

void display_control_present_rect(uint32_t x, uint32_t y,
                                  uint32_t width, uint32_t height) {
    const display_control_rect_t rect = {x, y, width, height};
    display_control_present_rects(&rect, 1U);
}

static void display_control_present_rects_locked(
        const display_control_rect_t *rects, uint32_t count) {
    if (!vmware_fifo || rects == NULL || count == 0U ||
        count > DISPLAY_CONTROL_PRESENT_CAPACITY) return;
    uint32_t commands[DISPLAY_CONTROL_PRESENT_CAPACITY * 5U];
    uint32_t command_count = 0U;
    for (uint32_t index = 0U; index < count; ++index) {
        if (rects[index].width == 0U || rects[index].height == 0U) continue;
        commands[command_count++] = SVGA_CMD_UPDATE;
        commands[command_count++] = rects[index].x;
        commands[command_count++] = rects[index].y;
        commands[command_count++] = rects[index].width;
        commands[command_count++] = rects[index].height;
    }
    if (command_count == 0U) return;
    uint32_t flags = spinlock_acquire_irq(&vmware_fifo_lock);
    bool submitted = vmware_fifo_write_batch(commands, command_count);
    bool doorbell = submitted && vmware_fifo_doorbell_needed();
    /* FIFO metadata is committed before the doorbell. VMware may trap the
     * SYNC port access into the host, so it must never extend the raw
     * spinlock/IRQ critical section and stall an unrelated cursor update. */
    spinlock_release_irq(&vmware_fifo_lock, flags);
    if (doorbell)
        svga_write(vmware_index_port, vmware_value_port, SVGA_REG_SYNC, 1U);
}

void display_control_present_rects(const display_control_rect_t *rects,
                                   uint32_t count) {
    if (kernel_mutex_lock_for(&display_state_mutex,
                              DISPLAY_STATE_TIMEOUT_MS) != 0) return;
    /* This owner executes only one fixed-capacity FIFO publication.  Keeping
     * it on its vCPU prevents a timer switch from stranding the sleepable
     * cross-CPU state mutex for an entire scheduling round. */
    scheduler_preempt_disable();
    display_control_present_rects_locked(rects, count);
    kernel_mutex_unlock(&display_state_mutex);
    scheduler_preempt_enable();
}

int display_control_cursor_update(int32_t x, int32_t y, bool visible) {
    /* Cursor publication is a latency-sensitive fixed-size operation. Take
     * the recursive display-state mutex while preemption is disabled, so an
     * owner transition returns KERNEL_MUTEX_WOULD_BLOCK before the pointer
     * state or scanout can change. framebuffer_cursor_update() re-enters the
     * mutex only for its one bounded present batch. */
    scheduler_preempt_disable();
    int lock_result = kernel_mutex_trylock_pinned(&display_state_mutex);
    if (lock_result != 0) {
        scheduler_preempt_enable();
        return lock_result;
    }
    bool updated = framebuffer_cursor_update(x, y, visible);
    kernel_mutex_unlock(&display_state_mutex);
    scheduler_preempt_enable();
    return updated ? 0 : -19;
}

static bool vmware_rect_valid(uint32_t x, uint32_t y,
                              uint32_t width, uint32_t height) {
    return active_backend == DISPLAY_BACKEND_VMWARE && vmware_fifo != NULL &&
        width != 0U && height != 0U && x < vmware_width && y < vmware_height &&
        width <= vmware_width - x && height <= vmware_height - y;
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

static pci_device_t *find_nvidia_gk208(void) {
    for (size_t index = 0U; index < pci_device_count; ++index) {
        pci_device_t *device = &pci_devices[index];
        if (device->vendor_id == NVIDIA_VENDOR &&
            device->device_id == NVIDIA_GK208_DEVICE &&
            device->class_code == 0x03U && device->subclass_code == 0x00U)
            return device;
    }
    return NULL;
}

static void prepare_nvidia_gk208(void) {
    pci_device_t *device = find_nvidia_gk208();
    pci_bar_info_t bar0;
    if (device == NULL ||
        !pci_describe_bar(device, 0U, &bar0) ||
        (bar0.flags & PCI_BAR_INFO_MMIO) == 0U ||
        (bar0.flags & PCI_BAR_INFO_PIO) != 0U || bar0.base_high != 0U ||
        bar0.base_low == 0U || bar0.size_high != 0U ||
        bar0.size_low < NVIDIA_REQUIRED_BAR_BYTES ||
        bar0.size_low > NVIDIA_BAR_MAX_BYTES)
        return;
    nvidia_prepared = true;
    printf("REIST_VIDEO NVIDIA_GK208_BAR_ADMITTED bytes=%u\n",
           (unsigned)bar0.size_low);
}

static pci_device_t *find_vmware_vga(void) {
    for (size_t index = 0; index < pci_device_count; ++index) {
        pci_device_t *device = &pci_devices[index];
        if (device->vendor_id == VMWARE_VENDOR &&
            (device->device_id == VMWARE_DEVICE_SVGA2 ||
             device->device_id == VMWARE_DEVICE_SVGA) &&
            device->class_code == 0x03U) return device;
    }
    return NULL;
}

static pci_device_t *find_vmware_display(void) {
    for (size_t index = 0; index < pci_device_count; ++index) {
        pci_device_t *device = &pci_devices[index];
        if (device->vendor_id == VMWARE_VENDOR &&
            device->class_code == 0x03U) return device;
    }
    return NULL;
}

static void report_unsupported_graphics(void) {
    uint32_t reported = 0U;
    for (size_t index = 0; index < pci_device_count; ++index) {
        pci_device_t *device = &pci_devices[index];
        if (device->class_code != 0x03U) continue;
        printf("DISPLAY_CONTROL: PCI %u:%u.%u VGA=%04X:%04X "
               "class=%02X:%02X prog-if=%02X "
               "BAR=%08X,%08X,%08X,%08X,%08X,%08X\n",
               (unsigned)device->bus, (unsigned)device->slot,
               (unsigned)device->function, (unsigned)device->vendor_id,
               (unsigned)device->device_id, (unsigned)device->class_code,
               (unsigned)device->subclass_code, (unsigned)device->prog_if,
               device->bar[0], device->bar[1], device->bar[2],
               device->bar[3], device->bar[4], device->bar[5]);
        reported++;
    }
    if (reported == 0U)
        printf("DISPLAY_CONTROL: no PCI display-class device detected\n");
}

static bool vbe_channel_valid(uint8_t position, uint8_t size) {
    return size != 0U && size <= 32U && position < 32U &&
           (uint16_t)position + size <= 32U;
}

static bool pci_memory_bar_size(pci_device_t *device, uint32_t bar_index,
                                uint64_t *base_out, uint64_t *size_out) {
    uint32_t low = device->bar[bar_index];
    if (low == 0U || low == UINT32_MAX || (low & 1U) != 0U)
        return false;
    bool is_64_bit = (low & 0x06U) == 0x04U;
    if ((!is_64_bit && (low & 0x06U) != 0U) ||
        (is_64_bit && bar_index + 1U >= 6U))
        return false;
    uint32_t high = is_64_bit ? device->bar[bar_index + 1U] : 0U;
    uint8_t offset = (uint8_t)(0x10U + bar_index * 4U);
    uint16_t command = pci_read_config_word(device->bus, device->slot,
                                             device->function, PCI_COMMAND);
    uint32_t old_flags;
    __asm__ __volatile__("pushf\n pop %0\n cli" : "=r"(old_flags) :: "memory");
    pci_write(device->bus, device->slot, device->function, PCI_COMMAND, 2U,
              command & (uint16_t)~PCI_COMMAND_MEMORY);
    pci_write(device->bus, device->slot, device->function, offset, 4U,
              0xFFFFFFFFU);
    if (is_64_bit)
        pci_write(device->bus, device->slot, device->function,
                  (uint8_t)(offset + 4U), 4U, 0xFFFFFFFFU);
    uint32_t mask_low = pci_read_config_dword(device->bus, device->slot,
                                               device->function, offset);
    uint32_t mask_high = is_64_bit
        ? pci_read_config_dword(device->bus, device->slot, device->function,
                                (uint8_t)(offset + 4U))
        : 0U;
    if (is_64_bit)
        pci_write(device->bus, device->slot, device->function,
                  (uint8_t)(offset + 4U), 4U, high);
    pci_write(device->bus, device->slot, device->function, offset, 4U, low);
    bool bars_restored =
        pci_read_config_dword(device->bus, device->slot, device->function,
                              offset) == low &&
        (!is_64_bit ||
         pci_read_config_dword(device->bus, device->slot, device->function,
                               (uint8_t)(offset + 4U)) == high);
    pci_write(device->bus, device->slot, device->function, PCI_COMMAND, 2U,
              command);
    bool command_restored =
        pci_read_config_word(device->bus, device->slot, device->function,
                             PCI_COMMAND) == command;
    __asm__ __volatile__("push %0\n popf" :: "r"(old_flags) : "memory");
    if (!bars_restored || !command_restored) return false;

    uint64_t base = ((uint64_t)high << 32U) | (low & 0xFFFFFFF0U);
    uint64_t mask = ((uint64_t)mask_high << 32U) |
                    (mask_low & 0xFFFFFFF0U);
    uint64_t bar_size = is_64_bit
        ? (~mask + 1U)
        : (((~mask) & UINT32_MAX) + 1U);
    if (bar_size == 0U || (bar_size & (bar_size - 1U)) != 0U ||
        (base & (bar_size - 1U)) != 0U)
        return false;
    *base_out = base;
    *size_out = bar_size;
    return true;
}

static bool vbe_address_matches_display_bar(uint32_t address,
                                            uint64_t length) {
    for (size_t device_index = 0U; device_index < pci_device_count;
         ++device_index) {
        pci_device_t *device = &pci_devices[device_index];
        if (device->class_code != 0x03U) continue;
        uint32_t candidate_index = UINT32_MAX;
        uint64_t candidate_base = 0U;
        for (uint32_t bar_index = 0U; bar_index < 6U; ++bar_index) {
            uint32_t low = device->bar[bar_index];
            if (low == 0U || (low & 1U) != 0U) continue;
            uint64_t base = low & 0xFFFFFFF0U;
            if ((low & 0x06U) == 0x04U) {
                if (bar_index + 1U >= 6U) continue;
                base |= (uint64_t)device->bar[bar_index + 1U] << 32U;
            } else if ((low & 0x06U) != 0U) {
                continue;
            }
            if (base <= address && base >= candidate_base) {
                candidate_index = bar_index;
                candidate_base = base;
            }
            if ((low & 0x06U) == 0x04U) bar_index++;
        }
        if (candidate_index != UINT32_MAX) {
            uint64_t bar_base;
            uint64_t bar_size;
            if (!pci_memory_bar_size(device, candidate_index, &bar_base,
                                     &bar_size))
                return false;
            uint64_t bar_end = bar_base + bar_size;
            uint64_t range_end = (uint64_t)address + length;
            if (bar_end >= bar_base && range_end >= address &&
                address >= bar_base && range_end <= bar_end) {
                printf("DISPLAY_CONTROL: VBE LFB inside BAR%u "
                       "VGA=%04X:%04X base=%08X size=%u MiB\n",
                       (unsigned)candidate_index,
                       (unsigned)device->vendor_id,
                       (unsigned)device->device_id, (unsigned)bar_base,
                       (unsigned)(bar_size / (1024U * 1024U)));
                return true;
            }
        }
    }
    return false;
}

static void prepare_vbe_handoff(void) {
    vbe_runtime_info_t info;
    memcpy(&info, (const void *)(uintptr_t)VBE_RUNTIME_INFO_ADDRESS,
           sizeof(info));
    vbe_runtime_info = info;
    vbe_reject_reason = 1U;
    if (info.magic != VBE_RUNTIME_INFO_MAGIC ||
        info.version != VBE_RUNTIME_INFO_VERSION ||
        info.struct_size != sizeof(info) || info.reserved != 0U)
        return;
    vbe_reject_reason = 2U;
    if (info.mode == 0U || info.mode == UINT16_MAX ||
        info.framebuffer_address == 0U)
        return;
    vbe_reject_reason = 3U;
    if (!((info.width == 1024U && info.height == 768U) ||
          (info.width == 800U && info.height == 600U)) ||
        info.pitch < info.width * 4U)
        return;
    vbe_reject_reason = 4U;
    if (info.bpp != 32U || info.memory_type != 1U)
        return;
    vbe_reject_reason = 5U;
    if (
        !vbe_channel_valid(info.red_position, info.red_size) ||
        !vbe_channel_valid(info.green_position, info.green_size) ||
        !vbe_channel_valid(info.blue_position, info.blue_size))
        return;
    uint64_t red = (((uint64_t)1U << info.red_size) - 1U) <<
                   info.red_position;
    uint64_t green = (((uint64_t)1U << info.green_size) - 1U) <<
                     info.green_position;
    uint64_t blue = (((uint64_t)1U << info.blue_size) - 1U) <<
                    info.blue_position;
    uint64_t length = (uint64_t)info.pitch * info.height;
    uint64_t end = (uint64_t)info.framebuffer_address + length;
    vbe_reject_reason = 6U;
    if ((red & green) != 0U || (red & blue) != 0U ||
        (green & blue) != 0U || length == 0U ||
        length > 64U * 1024U * 1024U || end > 0x100000000ULL ||
        (end > 0x40000000ULL && info.framebuffer_address < 0xC0000000U))
        return;
    vbe_reject_reason = 7U;
    if (!vbe_address_matches_display_bar(info.framebuffer_address, length))
        return;
    vbe_reject_reason = 8U;
    if (map_mmio_region(info.framebuffer_address, (size_t)length) == NULL)
        return;
    vbe_reject_reason = 0U;
    vbe_prepared = true;
}

void display_control_prepare(void) {
    uint32_t qemu_lfb = 0U;
    pci_device_t *qemu = find_qemu_vga(&qemu_lfb);
    if (qemu) {
        uint16_t id = dispi_read(DISPI_ID);
        uint16_t memory_64k = dispi_read(DISPI_VIDEO_MEMORY_64K);
        uint32_t length = (uint32_t)memory_64k * 65536U;
        if (id >= 0xB0C0U && id <= 0xB0C5U && memory_64k != 0U &&
            length <= 64U * 1024U * 1024U &&
            map_mmio_region(qemu_lfb, length) != NULL)
            qemu_prepared = true;
    }

    pci_device_t *vmware = find_vmware_vga();
    if (vmware) {
        vmware_supervised = true;
        uint32_t index_bar = vmware->bar[0];
        if ((index_bar & 1U) != 0U &&
            (index_bar & 0xFFFFFFFCU) != 0U) {
            uint16_t index_port = (uint16_t)(index_bar & 0xFFFCU);
            uint16_t value_port = (uint16_t)(index_port + 1U);
            pci_enable_device(vmware);
            svga_write(index_port, value_port, SVGA_REG_ID, SVGA_ID_2);
            uint32_t id = svga_read(index_port, value_port, SVGA_REG_ID);
            if (id != SVGA_ID_2) {
                svga_write(index_port, value_port, SVGA_REG_ID, SVGA_ID_1);
                id = svga_read(index_port, value_port, SVGA_REG_ID);
            }
            if (id == SVGA_ID_1 || id == SVGA_ID_2) {
                uint32_t framebuffer_start =
                    svga_read(index_port, value_port, SVGA_REG_FB_START);
                uint32_t framebuffer_size =
                    svga_read(index_port, value_port, SVGA_REG_FB_SIZE);
                uint32_t fifo_start =
                    svga_read(index_port, value_port, SVGA_REG_MEM_START);
                uint32_t fifo_size =
                    svga_read(index_port, value_port, SVGA_REG_MEM_SIZE);
                printf("REIST_VIDEO SVGA2D_PROBE id=%08X "
                       "fb=%08X/%u fifo=%08X/%u bars=%08X,%08X,%08X\n",
                       id, framebuffer_start, framebuffer_size,
                       fifo_start, fifo_size, vmware->bar[0],
                       vmware->bar[1], vmware->bar[2]);
                if (framebuffer_start == 0U)
                    framebuffer_start = vmware->bar[1] & 0xFFFFFFF0U;
                if (fifo_start == 0U)
                    fifo_start = vmware->bar[2] & 0xFFFFFFF0U;
                if (framebuffer_start != 0U &&
                    framebuffer_size >= 800U * 600U * 4U &&
                    framebuffer_size <= 64U * 1024U * 1024U &&
                    fifo_start != 0U && fifo_size >= 4096U &&
                    fifo_size <= 16U * 1024U * 1024U &&
                    /* Establish the scanout cache type on its FIRST mapping.
                     * A UC probe mapping cannot later be promoted to WC by
                     * framebuffer activation: conflicting mappings fail closed.
                     * Command FIFO/register memory must remain uncached. */
                    (map_kernel_write_combining(framebuffer_start,
                                                framebuffer_size) != NULL ||
                     map_mmio_region(framebuffer_start,
                                     framebuffer_size) != NULL) &&
                    map_mmio_region(fifo_start, fifo_size) != NULL)
                    vmware_prepared = true;
            }
        }
    }
    prepare_nvidia_gk208();
    prepare_vbe_handoff();
}

static int activate_vbe(void) {
    if (!vbe_prepared) return -19;
    if (vbe_runtime_set_mode(vbe_runtime_info.mode) != 0) {
        printf("DISPLAY_CONTROL: VBE mode switch failed mode=%X\n",
               (unsigned)vbe_runtime_info.mode);
        return -19;
    }
    multiboot_framebuffer_info_t info = {
        .framebuffer_addr = vbe_runtime_info.framebuffer_address,
        .framebuffer_pitch = vbe_runtime_info.pitch,
        .framebuffer_width = vbe_runtime_info.width,
        .framebuffer_height = vbe_runtime_info.height,
        .framebuffer_bpp = vbe_runtime_info.bpp,
        .framebuffer_type = vbe_runtime_info.memory_type,
        .red_field_position = vbe_runtime_info.red_position,
        .red_mask_size = vbe_runtime_info.red_size,
        .green_field_position = vbe_runtime_info.green_position,
        .green_mask_size = vbe_runtime_info.green_size,
        .blue_field_position = vbe_runtime_info.blue_position,
        .blue_mask_size = vbe_runtime_info.blue_size
    };
    framebuffer_init_runtime(&info);
    if (!framebuffer_available()) {
        printf("DISPLAY_CONTROL: VBE framebuffer publication failed\n");
        (void)vbe_runtime_set_text_mode();
        return -19;
    }
    active_backend = DISPLAY_BACKEND_VBE;
    printf("DISPLAY_CONTROL: VBE mode=%X framebuffer=%08X %ux%u ready\n",
           (unsigned)vbe_runtime_info.mode,
           vbe_runtime_info.framebuffer_address,
           (unsigned)vbe_runtime_info.width,
           (unsigned)vbe_runtime_info.height);
    return 0;
}

#ifndef REIST_VBE_RUNTIME_TEST
static int activate_vmware(pci_device_t *device, uint32_t requested_width,
                           uint32_t requested_height) {
    if (mode_fault_backend != DISPLAY_BACKEND_NONE) return -5;
    if (!vmware_prepared) return -19;
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
    uint32_t width = requested_width ? requested_width : max_width >= 1024U ? 1024U : 800U;
    uint32_t height = requested_height ? requested_height : max_height >= 768U ? 768U : 600U;
    uint64_t required = (uint64_t)width * height * 4U;
    uint32_t fb_size = svga_read(index_port, value_port, SVGA_REG_FB_SIZE);
    printf("REIST_VIDEO SVGA2D_MODE max=%ux%u fbsize=%u\n",
           (unsigned)max_width, (unsigned)max_height, (unsigned)fb_size);
    if (max_width < 800U || max_height < 600U || required > fb_size ||
        !reist_display_geometry_fits(width, height, width * 4U,
            max_width, max_height, fb_size, FB_SHADOW_CAPACITY)) return -19;
    pci_enable_device(device);
    svga_write(index_port, value_port, SVGA_REG_ENABLE, 0U);
    if (svga_read(index_port, value_port, SVGA_REG_ENABLE) & SVGA_ENABLE)
        goto vmware_disable;
    svga_write(index_port, value_port, SVGA_REG_WIDTH, width);
    svga_write(index_port, value_port, SVGA_REG_HEIGHT, height);
    svga_write(index_port, value_port, SVGA_REG_BITS_PER_PIXEL, 32U);
    svga_write(index_port, value_port, SVGA_REG_ENABLE, SVGA_ENABLE);
    if (svga_read(index_port, value_port, SVGA_REG_WIDTH) != width ||
        svga_read(index_port, value_port, SVGA_REG_HEIGHT) != height ||
        svga_read(index_port, value_port, SVGA_REG_BITS_PER_PIXEL) != 32U ||
        (svga_read(index_port, value_port, SVGA_REG_ENABLE) & SVGA_ENABLE) == 0U)
        goto vmware_disable;
    uint32_t pitch = svga_read(index_port, value_port, SVGA_REG_BYTES_PER_LINE);
    uint32_t framebuffer_start =
        svga_read(index_port, value_port, SVGA_REG_FB_START);
    uint32_t framebuffer_offset =
        svga_read(index_port, value_port, SVGA_REG_FB_OFFSET);
    uint64_t visible_bytes = (uint64_t)pitch * height;
    uint64_t framebuffer_address =
        (uint64_t)framebuffer_start + framebuffer_offset;
    printf("REIST_VIDEO SVGA2D_SCANOUT pitch=%u start=%08X offset=%u\n",
           (unsigned)pitch, framebuffer_start, (unsigned)framebuffer_offset);
    if (pitch < width * 4U || framebuffer_start == 0U ||
        framebuffer_start != (framebuffer_bar & 0xFFFFFFF0U) ||
        framebuffer_address > UINT32_MAX ||
        framebuffer_offset > fb_size ||
        visible_bytes > (uint64_t)fb_size - framebuffer_offset ||
        visible_bytes > FB_SHADOW_CAPACITY)
        goto vmware_disable;
    uint32_t fifo_start = svga_read(index_port, value_port, SVGA_REG_MEM_START);
    uint32_t fifo_size = svga_read(index_port, value_port, SVGA_REG_MEM_SIZE);
    uint32_t fifo_registers =
        svga_read(index_port, value_port, SVGA_REG_MEM_REGS);
    /* SVGA-II ID2 permits legacy implementations to omit MEM_REGS.  The
     * mandatory FIFO header is still MIN/MAX/NEXT_CMD/STOP (four dwords). */
    if (fifo_registers == 0U) fifo_registers = 4U;
    uint32_t fifo_minimum = fifo_registers * sizeof(uint32_t);
    printf("REIST_VIDEO SVGA2D_FIFO start=%08X size=%u regs=%u\n",
           fifo_start, (unsigned)fifo_size, (unsigned)fifo_registers);
    if (fifo_start == 0U) fifo_start = fifo_bar & 0xFFFFFFF0U;
    if (fifo_start != (fifo_bar & 0xFFFFFFF0U) ||
        fifo_size < 4096U || fifo_size > 16U*1024U*1024U || fifo_registers < 4U ||
        fifo_registers > fifo_size / sizeof(uint32_t) ||
        fifo_minimum > fifo_size - 5U * sizeof(uint32_t))
        goto vmware_disable;
    volatile uint32_t *fifo = map_mmio_region(fifo_start, fifo_size);
    if (!fifo) goto vmware_disable;
    fifo[SVGA_FIFO_MIN] = fifo_minimum;
    fifo[SVGA_FIFO_MAX] = fifo_size;
    fifo[SVGA_FIFO_NEXT_CMD] = fifo_minimum;
    fifo[SVGA_FIFO_STOP] = fifo_minimum;
    if (fifo_minimum >= (SVGA_FIFO_BUSY + 1U) * sizeof(uint32_t))
        fifo[SVGA_FIFO_BUSY] = 0U;
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
    framebuffer_init_runtime(&info);
    if (!framebuffer_available()) goto vmware_disable_fifo;
    vmware_fifo = fifo;
    vmware_index_port = index_port;
    vmware_value_port = value_port;
    vmware_capabilities = svga_read(
        index_port, value_port, SVGA_REG_CAPABILITIES) &
        (SVGA_CAP_RECT_FILL | SVGA_CAP_RECT_COPY);
    vmware_width = width;
    vmware_height = height;
    active_backend = DISPLAY_BACKEND_VMWARE;
    printf("REIST_VIDEO SVGA2D_ACTIVE caps=%X geometry=%ux%u\n",
           (unsigned)vmware_capabilities, (unsigned)width, (unsigned)height);
    return 0;

vmware_disable_fifo:
    svga_write(index_port, value_port, SVGA_REG_CONFIG_DONE, 0U);
vmware_disable:
    svga_write(index_port, value_port, SVGA_REG_ENABLE, 0U);
    if ((svga_read(index_port, value_port, SVGA_REG_ENABLE) & SVGA_ENABLE) != 0U) {
        mode_fault_backend = DISPLAY_BACKEND_VMWARE;
        mode_fault_index = index_port; mode_fault_value = value_port;
        return -5;
    }
    return -19;
}
#endif

static int display_control_activate_locked(uint32_t requested_width, uint32_t requested_height) {
    if (mode_fault_backend != DISPLAY_BACKEND_NONE) return -5;
    /* A framebuffer published by the BIOS loader does not prove that its
     * graphics mode is still the visible hardware mode.  In particular the
     * rescue shell may have restored VGA text while the bounded framebuffer
     * metadata remains available.  Only an explicitly active runtime backend
     * may make activation idempotent. */
    if (active_backend != DISPLAY_BACKEND_NONE && framebuffer_available())
        return 0;
    printf("DISPLAY_CONTROL: activation requested\n");
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
#ifdef REIST_VBE_RUNTIME_TEST
    pci_device_t *device = NULL;
#else
    pci_device_t *device = find_qemu_vga(&lfb_address);
    if (!device) {
        pci_device_t *candidate = find_vmware_vga();
        if (candidate) {
            if (vmware_supervised) {
                printf("DISPLAY_CONTROL: VMware activation requires "
                       "supervised svga2d-ring3\n");
                result = -13;
            } else {
                result = activate_vmware(candidate, requested_width, requested_height);
            }
            goto activation_done;
        }
        candidate = find_vmware_display();
        if (candidate) {
            printf("DISPLAY_CONTROL: unsupported VMware VGA=%04X:%04X; "
                   "VBE runtime transition suppressed\n",
                   (unsigned)candidate->vendor_id,
                   (unsigned)candidate->device_id);
            goto activation_done;
        }
    }
#endif
    uint16_t id = dispi_read(DISPI_ID);
    uint16_t memory_64k = dispi_read(DISPI_VIDEO_MEMORY_64K);
    printf("DISPLAY_CONTROL: DISPI probe pci=%u prepared=%u id=%X memory64k=%u\n",
           device != NULL ? 1U : 0U, qemu_prepared ? 1U : 0U,
           (unsigned)id, (unsigned)memory_64k);
    bool supported_id = id >= 0xB0C0U && id <= 0xB0C5U;
    uint32_t width = requested_width ? requested_width : memory_64k >= 48U ? 1024U : 800U;
    uint32_t height = requested_height ? requested_height : memory_64k >= 48U ? 768U : 600U;
    uint64_t required = (uint64_t)width * height * 4U;
    bool valid = device != NULL && qemu_prepared && supported_id &&
                 memory_64k != 0U &&
                 required <= (uint64_t)memory_64k * 65536U &&
                 lfb_address != 0U && (width & 7U) == 0U &&
                 reist_display_geometry_fits(width, height, width * 4U,
                    REIST_DISPLAY_MODE_MAX_DIMENSION, REIST_DISPLAY_MODE_MAX_DIMENSION,
                    (uint32_t)memory_64k * 65536U, FB_SHADOW_CAPACITY);
    if (valid) {
        printf("DISPLAY_CONTROL: QEMU DISPI transition\n");
        pci_enable_device(device);
        dispi_write(DISPI_ENABLE, 0U);
        if (dispi_read(DISPI_ENABLE) & DISPI_ENABLED) {
            mode_fault_backend = DISPLAY_BACKEND_QEMU;
            result = -5;
            goto activation_finished;
        }
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
                dispi_read(DISPI_VIRT_WIDTH) == width &&
                dispi_read(DISPI_X_OFFSET) == 0U && dispi_read(DISPI_Y_OFFSET) == 0U &&
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
            framebuffer_init_runtime(&info);
            if (framebuffer_available()) {
                active_backend = DISPLAY_BACKEND_QEMU;
                result = 0;
                printf("DISPLAY_CONTROL: QEMU framebuffer ready\n");
            }
        }
        if (result != 0) {
            dispi_write(DISPI_ENABLE, 0U);
            if ((dispi_read(DISPI_ENABLE) & DISPI_ENABLED) != 0U) {
                mode_fault_backend = DISPLAY_BACKEND_QEMU;
                result = -5;
            }
        }
    }
    if (result != 0 && mode_fault_backend == DISPLAY_BACKEND_NONE && vbe_prepared &&
        (!requested_width || (requested_width == vbe_runtime_info.width &&
                              requested_height == vbe_runtime_info.height)))
        result = activate_vbe();
#ifndef REIST_VBE_RUNTIME_TEST
activation_done:
#endif
activation_finished:
    if (result != 0) {
        if (!vbe_prepared) {
            printf("DISPLAY_CONTROL: VBE rejected reason=%u "
                   "magic=%08X version=%u size=%u mode=%X "
                   "lfb=%08X pitch=%u geometry=%ux%u bpp=%u type=%u\n",
                   (unsigned)vbe_reject_reason, vbe_runtime_info.magic,
                   (unsigned)vbe_runtime_info.version,
                   (unsigned)vbe_runtime_info.struct_size,
                   (unsigned)vbe_runtime_info.mode,
                   vbe_runtime_info.framebuffer_address,
                   (unsigned)vbe_runtime_info.pitch,
                   (unsigned)vbe_runtime_info.width,
                   (unsigned)vbe_runtime_info.height,
                   (unsigned)vbe_runtime_info.bpp,
                   (unsigned)vbe_runtime_info.memory_type);
        }
        report_unsupported_graphics();
        printf("DISPLAY_CONTROL: native graphics unavailable\n");
    }
    __asm__ __volatile__("cli" ::: "memory");
    activation_busy = false;
    __asm__ __volatile__("push %0\n popf" :: "r"(old_flags) : "memory");
    return result;
}

/* Pure metadata query under the display mutex. Indexed-register selection
 * does not enable an engine, alter mode or grant a raw device mapping. */
static int display_control_mode_query_locked(reist_display_mode_request_t *out) {
    reist_display_mode_request_t result = {0};
    result.version = REIST_DISPLAY_MODE_VERSION;
    result.struct_size = sizeof(result);
    result.operation = REIST_DISPLAY_MODE_QUERY;
    result.bpp = 32U;
    result.shadow_bytes = FB_SHADOW_CAPACITY;
    uint32_t address = 0U;
#ifndef REIST_VBE_RUNTIME_TEST
    pci_device_t *device = find_qemu_vga(&address);
    if (device && qemu_prepared) {
        uint16_t id = dispi_read(DISPI_ID);
        uint32_t bytes = (uint32_t)dispi_read(DISPI_VIDEO_MEMORY_64K) * 65536U;
        if (id < 0xB0C0U || id > 0xB0C5U || !address || !bytes || bytes > 64U*1024U*1024U)
            return -19;
        result.backend = REIST_DISPLAY_BACKEND_DISPI;
        result.max_width = result.max_height = REIST_DISPLAY_MODE_MAX_DIMENSION;
        result.scanout_bytes = bytes;
    } else if ((device = find_vmware_vga()) != NULL && vmware_prepared) {
        uint16_t index = (uint16_t)(device->bar[0] & 0xFFFCU);
        uint16_t value = (uint16_t)(index + 1U);
        result.backend = REIST_DISPLAY_BACKEND_SVGA2;
        result.max_width = svga_read(index, value, SVGA_REG_MAX_WIDTH);
        result.max_height = svga_read(index, value, SVGA_REG_MAX_HEIGHT);
        result.scanout_bytes = svga_read(index, value, SVGA_REG_FB_SIZE);
        if (!result.scanout_bytes || result.scanout_bytes > 64U*1024U*1024U) return -19;
    } else if (find_vmware_display() != NULL) return -19;
    else
#else
    (void)address;
#endif
    if (vbe_prepared) {
        uint64_t bytes = (uint64_t)vbe_runtime_info.pitch * vbe_runtime_info.height;
        if (!bytes || bytes > FB_SHADOW_CAPACITY) return -19;
        result.backend = REIST_DISPLAY_BACKEND_VBE;
        result.fixed_width = result.max_width = vbe_runtime_info.width;
        result.fixed_height = result.max_height = vbe_runtime_info.height;
        result.scanout_bytes = (uint32_t)bytes;
    } else return -19;
    if (result.max_width > REIST_DISPLAY_MODE_MAX_DIMENSION)
        result.max_width = REIST_DISPLAY_MODE_MAX_DIMENSION;
    if (result.max_height > REIST_DISPLAY_MODE_MAX_DIMENSION)
        result.max_height = REIST_DISPLAY_MODE_MAX_DIMENSION;
    framebuffer_display_info_t current;
    if (active_backend != DISPLAY_BACKEND_NONE && framebuffer_get_display_info(&current)) {
        result.flags = REIST_DISPLAY_MODE_ACTIVE;
        result.width = current.width; result.height = current.height;
    }
    *out = result;
    return 0;
}

int display_control_mode_query(reist_display_mode_request_t *request) {
    if (!request) return -22;
    int status = kernel_mutex_lock_for(&display_state_mutex, DISPLAY_STATE_TIMEOUT_MS);
    if (status != 0) return status;
    status = display_control_mode_query_locked(request);
    kernel_mutex_unlock(&display_state_mutex);
    return status;
}

/* Caller already owns the display authority. A new mode is startup-only;
 * not even a valid request can resize a live session behind its consumers. */
static int display_control_mode_admit_locked(uint32_t width, uint32_t height,
                                            reist_display_mode_request_t *caps) {
    if (mode_fault_backend != DISPLAY_BACKEND_NONE) return -5;
    int status = display_control_mode_query_locked(caps);
    if (status != 0) return status;
    if (!reist_display_mode_supported(width, height, caps)) return -95;
    if (caps->flags & REIST_DISPLAY_MODE_ACTIVE)
        return caps->width == width && caps->height == height ? 1 : -16;
    return 0;
}

int display_control_activate_mode(uint32_t width, uint32_t height) {
    int status = kernel_mutex_lock_for(&display_state_mutex, DISPLAY_STATE_TIMEOUT_MS);
    if (status != 0) return status;
    reist_display_mode_request_t caps;
    status = display_control_mode_admit_locked(width, height, &caps);
    if (status == 0) {
        status = caps.backend == REIST_DISPLAY_BACKEND_SVGA2 ? -13 :
            display_control_activate_locked(width, height);
    } else if (status == 1) status = 0;
    kernel_mutex_unlock(&display_state_mutex);
    return status;
}

int display_control_activate(void) {
    int lock_result = kernel_mutex_lock_for(&display_state_mutex,
                                            DISPLAY_STATE_TIMEOUT_MS);
    if (lock_result != 0) return lock_result;
    int result = display_control_activate_locked(0U, 0U);
    kernel_mutex_unlock(&display_state_mutex);
    return result;
}

static int display_control_deactivate_locked(void) {
    uint32_t old_flags;
    __asm__ __volatile__("pushf\n pop %0\n cli" : "=r"(old_flags) :: "memory");
    if (activation_busy) {
        __asm__ __volatile__("push %0\n popf" :: "r"(old_flags) : "memory");
        return -16;
    }
    activation_busy = true;
    __asm__ __volatile__("push %0\n popf" :: "r"(old_flags) : "memory");

    int result = -19;
    bool was_vmware = active_backend == DISPLAY_BACKEND_VMWARE;
    (void)framebuffer_cursor_update(0, 0, false);
    if (mode_fault_backend == DISPLAY_BACKEND_VMWARE) {
        svga_write(mode_fault_index, mode_fault_value, SVGA_REG_ENABLE, 0U);
        if ((svga_read(mode_fault_index, mode_fault_value,
                       SVGA_REG_ENABLE) & SVGA_ENABLE) == 0U) result = 0;
    } else if (active_backend == DISPLAY_BACKEND_QEMU ||
               mode_fault_backend == DISPLAY_BACKEND_QEMU) {
        dispi_write(DISPI_ENABLE, 0U);
        if ((dispi_read(DISPI_ENABLE) & DISPI_ENABLED) == 0U) result = 0;
    } else if (active_backend == DISPLAY_BACKEND_VMWARE &&
               vmware_index_port != 0U && vmware_value_port != 0U) {
        svga_write(vmware_index_port, vmware_value_port, SVGA_REG_ENABLE, 0U);
        if ((svga_read(vmware_index_port, vmware_value_port,
                       SVGA_REG_ENABLE) & SVGA_ENABLE) == 0U) result = 0;
    } else if (active_backend == DISPLAY_BACKEND_VBE) {
        result = vbe_runtime_set_text_mode();
    }

    if (result == 0) {
        framebuffer_shutdown();
        vmware_fifo = NULL;
        vmware_index_port = 0U;
        vmware_value_port = 0U;
        vmware_capabilities = 0U;
        vmware_width = 0U;
        vmware_height = 0U;
        vmware_rect_copy_reported = false;
        active_backend = DISPLAY_BACKEND_NONE;
        mode_fault_backend = DISPLAY_BACKEND_NONE;
        mode_fault_index = mode_fault_value = 0U;
        if (was_vmware) printf("REIST_VIDEO SVGA2D_INACTIVE\n");
    }
    __asm__ __volatile__("cli" ::: "memory");
    activation_busy = false;
    __asm__ __volatile__("push %0\n popf" :: "r"(old_flags) : "memory");
    return result;
}

int display_control_deactivate(void) {
    int lock_result = kernel_mutex_lock_for(&display_state_mutex,
                                            DISPLAY_STATE_TIMEOUT_MS);
    if (lock_result != 0) return lock_result;
    int result = display_control_deactivate_locked();
    kernel_mutex_unlock(&display_state_mutex);
    return result;
}

bool display_control_graphics_active(void) {
    if (kernel_mutex_lock_for(&display_state_mutex,
                              DISPLAY_STATE_TIMEOUT_MS) != 0) return false;
    scheduler_preempt_disable();
    bool active = mode_fault_backend != DISPLAY_BACKEND_NONE ||
        (active_backend != DISPLAY_BACKEND_NONE && framebuffer_available());
    kernel_mutex_unlock(&display_state_mutex);
    scheduler_preempt_enable();
    return active;
}

bool display_control_acceleration_active(void) {
    if (kernel_mutex_lock_for(&display_state_mutex,
                              DISPLAY_STATE_TIMEOUT_MS) != 0) return false;
    scheduler_preempt_disable();
    bool active = false;
    if (active_backend == DISPLAY_BACKEND_VMWARE) {
        active = vmware_fifo != NULL &&
            (vmware_capabilities & SVGA_CAP_RECT_COPY) != 0U;
    } else if (active_backend == DISPLAY_BACKEND_VBE) {
        active = device_domain_gr_acceleration_active();
    }
    kernel_mutex_unlock(&display_state_mutex);
    scheduler_preempt_enable();
    return active;
}

static int display_control_driver_command_locked(
        display_driver_request_t *request) {
    if (request == NULL) return -22;
    if (find_nvidia_gk208() != NULL) {
        int result = -95;
        if (request->command == DISPLAY_DRIVER_ACTIVATE) {
            result = activate_vbe();
            if (result == 0) {
                framebuffer_display_info_t info;
                if (framebuffer_get_display_info(&info)) {
                    request->width = info.width;
                    request->height = info.height;
                }
            }
        } else if (request->command == DISPLAY_DRIVER_DEACTIVATE) {
            result = active_backend == DISPLAY_BACKEND_VBE
                ? display_control_deactivate() : -19;
        } else if (request->command == DISPLAY_DRIVER_BUSY_QUERY) {
            request->busy = 0U;
            result = nvidia_prepared ? 0 : -19;
        }
        /* Capabilities deliberately remain zero until a hardware-completed
         * fence proves a bounded native GPFIFO path on the target card. */
        request->capabilities = 0U;
        request->status = result;
        return result;
    }
    int result = -22;
    bool non_destructive_probe =
        request->command == DISPLAY_DRIVER_PROBE ||
        request->command == DISPLAY_DRIVER_ENGINE_PREFLIGHT;
    if (non_destructive_probe) {
        pci_device_t *device = find_vmware_vga();
        if (device == NULL || !vmware_prepared) {
            result = -19;
        } else {
            uint32_t index_bar = device->bar[0];
            uint16_t index_port = (uint16_t)(index_bar & 0xFFFCU);
            uint16_t value_port = (uint16_t)(index_port + 1U);
            uint32_t max_width = svga_read(
                index_port, value_port, SVGA_REG_MAX_WIDTH);
            uint32_t max_height = svga_read(
                index_port, value_port, SVGA_REG_MAX_HEIGHT);
            uint32_t fifo_size = svga_read(
                index_port, value_port, SVGA_REG_MEM_SIZE);
            uint32_t fifo_start = svga_read(
                index_port, value_port, SVGA_REG_MEM_START);
            if (fifo_start == 0U)
                fifo_start = device->bar[2] & 0xFFFFFFF0U;
            if (max_width < 800U || max_height < 600U ||
                fifo_start == 0U || fifo_size < 4096U) {
                result = -19;
            } else {
                request->width = max_width >= 1024U ? 1024U : 800U;
                request->height = max_height >= 768U ? 768U : 600U;
                request->capabilities = svga_read(
                    index_port, value_port, SVGA_REG_CAPABILITIES) &
                    (SVGA_CAP_RECT_FILL | SVGA_CAP_RECT_COPY);
                request->busy = 0U;
                result = 0;
            }
        }
    } else if (request->command == DISPLAY_DRIVER_ACTIVATE ||
               request->command == DISPLAY_DRIVER_ACTIVATE_MODE) {
        pci_device_t *device = find_vmware_vga();
        uint32_t width = 0U, height = 0U;
        result = 0;
        if (request->command == DISPLAY_DRIVER_ACTIVATE_MODE) {
            reist_display_mode_request_t caps;
            if (request->source_x || request->source_y || request->destination_x ||
                request->destination_y || request->color) return -22;
            width = request->width; height = request->height;
            result = display_control_mode_admit_locked(width, height, &caps);
            if (result >= 0 && caps.backend != REIST_DISPLAY_BACKEND_SVGA2) result = -95;
        }
        if (result >= 0) result = active_backend == DISPLAY_BACKEND_VMWARE
            ? 0 : device != NULL ? activate_vmware(device, width, height) : -19;
    } else if (request->command == DISPLAY_DRIVER_DEACTIVATE) {
        result = active_backend == DISPLAY_BACKEND_VMWARE
            ? display_control_deactivate() : -19;
    } else if (request->command == DISPLAY_DRIVER_BUSY_QUERY) {
        if (active_backend != DISPLAY_BACKEND_VMWARE ||
            vmware_index_port == 0U || vmware_value_port == 0U) {
            result = -19;
        } else {
            request->busy = svga_read(vmware_index_port, vmware_value_port,
                                      SVGA_REG_BUSY) != 0U ? 1U : 0U;
            result = 0;
        }
    } else {
        uint32_t commands[7U];
        uint32_t command_count = 0U;
        if (request->command == DISPLAY_DRIVER_RECT_FILL) {
            if ((vmware_capabilities & SVGA_CAP_RECT_FILL) == 0U)
                return -95;
            if (!vmware_rect_valid(request->destination_x,
                                   request->destination_y,
                                   request->width, request->height))
                return -22;
            commands[command_count++] = SVGA_CMD_RECT_FILL;
            commands[command_count++] = request->color;
            commands[command_count++] = request->destination_x;
            commands[command_count++] = request->destination_y;
            commands[command_count++] = request->width;
            commands[command_count++] = request->height;
        } else if (request->command == DISPLAY_DRIVER_RECT_COPY) {
            if ((vmware_capabilities & SVGA_CAP_RECT_COPY) == 0U)
                return -95;
            if (!vmware_rect_valid(request->source_x, request->source_y,
                                   request->width, request->height) ||
                !vmware_rect_valid(request->destination_x,
                                   request->destination_y,
                                   request->width, request->height))
                return -22;
            commands[command_count++] = SVGA_CMD_RECT_COPY;
            commands[command_count++] = request->source_x;
            commands[command_count++] = request->source_y;
            commands[command_count++] = request->destination_x;
            commands[command_count++] = request->destination_y;
            commands[command_count++] = request->width;
            commands[command_count++] = request->height;
        }
        if (command_count != 0U) {
            uint32_t flags = spinlock_acquire_irq(&vmware_fifo_lock);
            bool submitted = vmware_fifo_write_batch(commands, command_count);
            bool doorbell = submitted && vmware_fifo_doorbell_needed();
            /* The FIFO write is atomic before the doorbell; do not retain a
             * raw spinlock across a VMware-trapped SYNC port access. */
            spinlock_release_irq(&vmware_fifo_lock, flags);
            if (doorbell)
                svga_write(vmware_index_port, vmware_value_port,
                           SVGA_REG_SYNC, 1U);
            result = submitted ? 0 : -11;
            if (submitted && request->command == DISPLAY_DRIVER_RECT_COPY &&
                !vmware_rect_copy_reported) {
                vmware_rect_copy_reported = true;
                printf("REIST_VIDEO SVGA2D_RECT_COPY_OK\n");
            }
        }
    }
    /* PROBE/PREFLIGHT intentionally run before a visible mode exists.  Keep
     * their locally measured geometry/capabilities instead of replacing the
     * response with the inactive runtime state. */
    if (!non_destructive_probe) {
        request->capabilities = vmware_capabilities;
        request->width = vmware_width;
        request->height = vmware_height;
    }
    request->status = result;
    return result;
}

int display_control_driver_command(display_driver_request_t *request) {
    if (request == NULL) return -22;
    bool bounded_command =
        request->command != DISPLAY_DRIVER_ACTIVATE &&
        request->command != DISPLAY_DRIVER_ACTIVATE_MODE &&
        request->command != DISPLAY_DRIVER_DEACTIVATE;
    int lock_result = kernel_mutex_lock_for(&display_state_mutex,
                                            DISPLAY_STATE_TIMEOUT_MS);
    if (lock_result != 0) {
        request->status = lock_result;
        return lock_result;
    }
    /* Probe, query and 2D submissions have fixed register/FIFO work and must
     * release display_state_mutex before their task can be timer-preempted.
     * Lifecycle commands can map memory and therefore remain sleepable. */
    if (bounded_command) scheduler_preempt_disable();
    int result = display_control_driver_command_locked(request);
    kernel_mutex_unlock(&display_state_mutex);
    if (bounded_command) scheduler_preempt_enable();
    return result;
}
