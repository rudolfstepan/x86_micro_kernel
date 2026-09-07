/* Production mode transaction with only hardware/lock/framebuffer doubles. */
#ifndef DISPLAY_RING3_HOST
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "drivers/bus/pci.h"
#include "drivers/video/framebuffer.h"
#include "drivers/video/display_control.h"
#include "arch/x86/boot/vbe_runtime.h"
/* DECLARATIONS */
static int display_state_mutex, lock_error;
#define DISPLAY_STATE_TIMEOUT_MS 1000U
static uint32_t registers[32], fifo_storage[1024];
static unsigned writes, enables, published, shutdowns;
static int hardware, wrong_readback, bad_pitch, stuck_disable, map_fail, init_fail;
static pci_device_t device;
static multiboot_framebuffer_info_t frame;
static uint32_t bar_bytes[3];
static unsigned probes, wc_maps, uc_maps;
static int probe_fail, wc_fail, current_extent;
static uint32_t extent_override, pitch_extra, shrink_on_enable;
static uint32_t last_scanout_map;
static bool pci_memory_bar_size(pci_device_t *d,uint32_t b,uint64_t *base,uint64_t *size) {
    assert(d==&device && (b==1 || b==2));++probes;
    *base=d->bar[b]&~15U;*size=bar_bytes[b];return !probe_fail;
}
static void prepare_nvidia_gk208(void) {}
static void prepare_vbe_handoff(void) {}
static int kernel_mutex_lock_for(int *mutex,uint32_t timeout) { (void)mutex;assert(timeout==1000U);return lock_error; }
static void kernel_mutex_unlock(int *mutex) { (void)mutex; }
static pci_device_t *find_qemu_vga(uint32_t *address) { *address=hardware==1 ? 0xE0000000U : 0;return hardware==1 ? &device : NULL; }
static pci_device_t *find_vmware_vga(void) { return hardware==2 ? &device : NULL; }
static pci_device_t *find_vmware_display(void) { return hardware==2 || hardware==4 ? &device : NULL; }
static uint16_t dispi_read(uint16_t index) {
    if (index==DISPI_XRES && wrong_readback && (registers[DISPI_ENABLE]&1)) return 800;
    return (uint16_t)registers[index];
}
static void dispi_write(uint16_t index,uint16_t value) {
    ++writes;
    if (index==DISPI_ENABLE && !value && stuck_disable) return;
    registers[index]=value;
}
static uint32_t svga_read(uint16_t i,uint16_t v,uint32_t index) {
    (void)i;(void)v;
    if (index==SVGA_REG_WIDTH && wrong_readback && registers[SVGA_REG_ENABLE]) return 800;
    if (index==SVGA_REG_BYTES_PER_LINE) return bad_pitch ? UINT32_MAX : registers[SVGA_REG_WIDTH]*4U+pitch_extra;
    if (index==SVGA_REG_FB_SIZE && current_extent && registers[SVGA_REG_ENABLE])
        return extent_override ? extent_override :
            (registers[SVGA_REG_WIDTH]*4U+pitch_extra)*registers[SVGA_REG_HEIGHT];
    return registers[index];
}
static void svga_write(uint16_t i,uint16_t v,uint32_t index,uint32_t value) {
    (void)i;(void)v;++writes;
    if (index==SVGA_REG_ENABLE && !value && stuck_disable) return;
    registers[index]=value;
    if (index==SVGA_REG_ENABLE && value && shrink_on_enable)
        registers[SVGA_REG_VRAM_SIZE]=shrink_on_enable;
}
void pci_enable_device(pci_device_t *d) { assert(d==&device);++enables; }
volatile uint32_t *map_mmio_region(uint64_t base,size_t size) {
    assert(base);++uc_maps;
    if (base==(device.bar[2]&~15U)) assert(size==4096);
    else { assert(size<=FB_SHADOW_CAPACITY);last_scanout_map=(uint32_t)size; }
    return map_fail ? NULL : fifo_storage;
}
static void *map_kernel_write_combining(uint32_t base,size_t size) {
    assert(base==(device.bar[1]&~15U) && size<=FB_SHADOW_CAPACITY);
    ++wc_maps;last_scanout_map=(uint32_t)size;
    return map_fail || wc_fail ? NULL : fifo_storage;
}
bool framebuffer_available(void) { return published!=0 && !init_fail; }
void framebuffer_init_runtime(multiboot_framebuffer_info_t *info) {
    assert(reist_display_geometry_fits(info->framebuffer_width,info->framebuffer_height,
        info->framebuffer_pitch,4096,4096,16U*1024U*1024U,FB_SHADOW_CAPACITY));
    frame=*info;if (!init_fail) ++published;
    if (hardware==2) {
        uint64_t offset=info->framebuffer_addr-(device.bar[1]&~15U);
        assert(offset+(uint64_t)info->framebuffer_pitch*info->framebuffer_height<=registers[SVGA_REG_VRAM_SIZE]);
    }
}
void framebuffer_shutdown(void) { ++shutdowns;published=0; }
bool framebuffer_get_display_info(framebuffer_display_info_t *info) {
    if (!framebuffer_available()) return false;
    memset(info,0,sizeof(*info));info->width=frame.framebuffer_width;info->height=frame.framebuffer_height;return true;
}
bool framebuffer_cursor_update(int32_t x,int32_t y,bool visible) { (void)x;(void)y;(void)visible;return true; }
int vbe_runtime_set_text_mode(void) { return 0; }
static int activate_vbe(void) { active_backend=DISPLAY_BACKEND_VBE;return 0; }
static void report_unsupported_graphics(void) {}
/* PRODUCTION */
static void reset(int backend) {
    memset(registers,0,sizeof(registers));memset(&device,0,sizeof(device));
    hardware=backend;writes=enables=published=shutdowns=0;
    wrong_readback=bad_pitch=stuck_disable=map_fail=init_fail=lock_error=0;
    probes=wc_maps=uc_maps=last_scanout_map=0;
    probe_fail=wc_fail=current_extent=0;
    extent_override=pitch_extra=shrink_on_enable=0;
    memset(bar_bytes,0,sizeof(bar_bytes));memset(vmware_bars,0,sizeof(vmware_bars));
    vmware_aperture_bytes=vmware_fifo_aperture_bytes=0;
    qemu_prepared=vmware_prepared=true;vbe_prepared=vmware_supervised=false;
    active_backend=mode_fault_backend=DISPLAY_BACKEND_NONE;activation_busy=false;
    nvidia_prepared=false;(void)nvidia_prepared;(void)vmware_rect_copy_reported;
    if (backend==1) { registers[DISPI_ID]=0xB0C5;registers[DISPI_VIDEO_MEMORY_64K]=256; }
    if (backend==2) {
        device.bar[0]=0x501;device.bar[1]=0xE0000000U;device.bar[2]=0xF0000000U;
        registers[SVGA_REG_ID]=SVGA_ID_2;registers[SVGA_REG_MAX_WIDTH]=2560;
        registers[SVGA_REG_MAX_HEIGHT]=1600;registers[SVGA_REG_FB_SIZE]=16U*1024U*1024U;
        registers[SVGA_REG_FB_START]=0xE0000000U;registers[SVGA_REG_MEM_START]=0xF0000000U;
        registers[SVGA_REG_MEM_SIZE]=4096;registers[SVGA_REG_MEM_REGS]=4;
        registers[SVGA_REG_VRAM_SIZE]=16U*1024U*1024U;
        bar_bytes[1]=128U*1024U*1024U;bar_bytes[2]=4096;
        assert(vmware_probe_apertures(&device));probes=0;
    }
}
int main(void) {
    reist_display_mode_request_t caps;
    /* Workstation, unlike QEMU, reports only the CURRENT extent in FB_SIZE. */
    reset(2);registers[15]=128U*1024U*1024U;
    registers[SVGA_REG_FB_SIZE]=1024U*768U*4U;
    assert(!display_control_mode_query(&caps) && !writes && !published);
    assert(caps.scanout_bytes==128U*1024U*1024U);
    assert(reist_display_mode_supported(1920,1080,&caps));
    current_extent=1;
    assert(!activate_vmware(&device,1920,1080) && published==1);
    assert(frame.framebuffer_width==1920 && frame.framebuffer_height==1080);
    assert(!display_control_deactivate());
    assert(!activate_vmware(&device,800,600));
    /* A smaller current extent must not shrink the choices. */
    unsigned query_writes=writes;
    assert(!display_control_mode_query(&caps) && writes==query_writes && !probes);
    assert(reist_display_mode_supported(1920,1080,&caps));
    assert(!display_control_deactivate());
    assert(!activate_vmware(&device,1280,720));
    assert(!display_control_deactivate());
    /* Preparation maps a bounded WC span, even with large or absent FB_SIZE. */
    for (unsigned fallback=0;fallback<2;++fallback) {
        reset(2);vmware_prepared=false;wc_fail=(int)fallback;
        registers[SVGA_REG_VRAM_SIZE]=128U*1024U*1024U;
        registers[SVGA_REG_FB_SIZE]=0;
        display_control_prepare();
        assert(vmware_prepared && probes==2 && wc_maps==1 && uc_maps==1+fallback);
        assert(last_scanout_map==FB_SHADOW_CAPACITY && !published);
    }
    for (unsigned invalid=0;invalid<11;++invalid) {
        reset(2);vmware_prepared=false;
        switch (invalid) {
        case 0: probe_fail=1;break;
        case 1: registers[SVGA_REG_VRAM_SIZE]=0;break;
        case 2: registers[SVGA_REG_VRAM_SIZE]=256U*1024U*1024U;break;
        case 3: device.bar[1]|=1U;break;
        case 4: device.bar[1]=0xFC000000U;break; /* aperture end exceeds 4 GiB */
        case 5: device.bar[2]=device.bar[1];break;
        case 6: registers[SVGA_REG_FB_START]=0x100000U;break;
        case 7: registers[SVGA_REG_MEM_START]=0x100000U;break;
        case 8: registers[SVGA_REG_MEM_SIZE]=8192U;break;
        case 9: device.bar[0]=0x10001U;break;
        case 10: bar_bytes[1]=1024U*1024U;break;
        }
        display_control_prepare();assert(!vmware_prepared && !wc_maps && !uc_maps && !published);
    }
    reset(2);vmware_prepared=false;map_fail=1;display_control_prepare();
    assert(!vmware_prepared && !published);
    for (unsigned invalid=0;invalid<5;++invalid) {
        reset(2);
        switch (invalid) {
        case 0: device.bar[1]+=0x1000;break;
        case 1: registers[SVGA_REG_VRAM_SIZE]=0;break;
        case 2: registers[SVGA_REG_VRAM_SIZE]=UINT32_MAX;break;
        case 3: registers[SVGA_REG_MEM_SIZE]=8192;break;
        case 4: device.bar[0]+=4;break;
        }
        memset(&caps,0xA5,sizeof(caps));reist_display_mode_request_t saved=caps;
        assert(display_control_mode_query(&caps)==-19 && !memcmp(&caps,&saved,sizeof(caps)));
        assert(activate_vmware(&device,1920,1080)<0 && !writes && !enables && !published && !probes);
    }
    /* Current extent is checked only AFTER enable; include padding and offset. */
    reset(2);current_extent=1;pitch_extra=64;registers[SVGA_REG_FB_OFFSET]=4096;
    assert(!activate_vmware(&device,1920,1080));
    assert(frame.framebuffer_pitch==7744 && frame.framebuffer_addr==0xE0001000U);
    for (unsigned invalid=0;invalid<7;++invalid) {
        reset(2);current_extent=1;
        switch (invalid) {
        case 0: extent_override=1;break;
        case 1: extent_override=UINT32_MAX;break;
        case 2: registers[SVGA_REG_FB_OFFSET]=UINT32_MAX;break;
        case 3: registers[SVGA_REG_FB_OFFSET]=15U*1024U*1024U;break;
        case 4: pitch_extra=16384;break;
        case 5: shrink_on_enable=4U*1024U*1024U;break;
        case 6: init_fail=1;break;
        }
        assert(activate_vmware(&device,1920,1080)<0 && !published && !registers[SVGA_REG_ENABLE]);
    }
    reset(1);assert(!display_control_mode_query(&caps) && !writes && !published);
    assert(display_control_activate_mode(4096,4096)==-95 && !writes && !enables);
    assert(display_control_activate_mode(1366,768)==-95 && !writes);
    assert(!display_control_activate_mode(1920,1080) && published==1 && frame.framebuffer_width==1920);
    unsigned before=writes;assert(display_control_activate_mode(800,600)==-16 && writes==before);
    assert(!display_control_activate_mode(1920,1080) && writes==before);
    assert(!display_control_deactivate() && !published && shutdowns==1);
    assert(!display_control_activate() && frame.framebuffer_width==1024 && frame.framebuffer_height==768);
    reset(1);wrong_readback=1;assert(display_control_activate_mode(1280,720)<0 && !published && !registers[DISPI_ENABLE]);
    reset(1);stuck_disable=1;registers[DISPI_ENABLE]=1;
    assert(display_control_activate_mode(1280,720)==-5 && mode_fault_backend==DISPLAY_BACKEND_QEMU);
    before=writes;assert(display_control_activate()==-5 && writes==before && !published);
    stuck_disable=0;assert(!display_control_deactivate() && !mode_fault_backend);
    reset(1);lock_error=-110;assert(display_control_activate_mode(800,600)==-110 && !writes);
    reset(2);assert(!display_control_mode_query(&caps) && caps.backend==2 && !writes);
    assert(display_control_activate_mode(1280,720)==-13 && !writes);
    assert(display_control_mode_admit_locked(4096,4096,&caps)==-95 && !writes);
    assert(!activate_vmware(&device,1280,720) && published==1 && frame.framebuffer_width==1280);
    assert(!display_control_deactivate() && !published);
    reset(2);wrong_readback=1;assert(activate_vmware(&device,1280,720)<0 && !published && !registers[SVGA_REG_ENABLE]);
    reset(2);bad_pitch=1;assert(activate_vmware(&device,1280,720)<0 && !published && !registers[SVGA_REG_ENABLE]);
    reset(2);map_fail=1;assert(activate_vmware(&device,1280,720)<0 && !published && !registers[SVGA_REG_ENABLE]);
    reset(2);registers[SVGA_REG_FB_START]=0x100000U;
    assert(activate_vmware(&device,1280,720)<0 && !published && !registers[SVGA_REG_ENABLE]);
    reset(2);registers[SVGA_REG_MEM_START]=0x100000U;
    assert(activate_vmware(&device,1280,720)<0 && !published && !registers[SVGA_REG_ENABLE]);
    reset(2);stuck_disable=1;registers[SVGA_REG_ENABLE]=1;
    assert(activate_vmware(&device,1280,720)==-5 && !published && mode_fault_backend==DISPLAY_BACKEND_VMWARE);
    before=writes;assert(activate_vmware(&device,800,600)==-5 && before==writes);
    stuck_disable=0;assert(!display_control_deactivate() && !mode_fault_backend);
    reset(3);vbe_prepared=true;vbe_runtime_info=(vbe_runtime_info_t){.width=1024,.height=768,.pitch=4096};
    assert(!display_control_mode_query(&caps) && caps.backend==3 && !writes);
    assert(display_control_activate_mode(800,600)==-95 && !writes);
    reset(4);vbe_prepared=true;assert(display_control_mode_query(&caps)==-19 && !writes);
    puts("DISPLAY_TEST_OK");return 0;
}
#else
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "x86os.h"
#include "userspace/video/include/reist/svga2d.h"
#include "userspace/gui/compositor/desktop_surface_runtime.h"
/* RING3_DECLARATIONS */
static unsigned commands, kills, mode_activations, queries;
static uint32_t last_command;
static int driver_error, readback_error, memory_error;
static uint32_t owner_generation=7;
static uint64_t now;
typedef struct { char image_path[128]; } Process;
static Process process;
static Process *scheduler_current_process(void) { return &process; }
static int copy_from_user(void *to,const void *from,size_t n) { if (memory_error) return -1;memcpy(to,from,n);return 0; }
static int copy_to_user(void *to,const void *from,size_t n) { if (memory_error) return -1;memcpy(to,from,n);return 0; }
static int display_control_mode_query(reist_display_mode_request_t *r) { ++queries;r->backend=1;return 0; }
static int display_control_activate_mode(uint32_t w,uint32_t h) { ++mode_activations;assert(w==800 && h==600);return 0; }
int x86os_display_driver_command(x86os_display_driver_request_t *r) {
    ++commands;last_command=r->command;assert(r->device==66);
    if (r->command==X86OS_DISPLAY_DRIVER_ACTIVATE) { r->width=1024;r->height=768; }
    if (r->command==X86OS_DISPLAY_DRIVER_ACTIVATE_MODE && readback_error) r->width=1;
    if (r->command==X86OS_DISPLAY_DRIVER_DEACTIVATE) ++kills;
    return driver_error;
}
int x86os_monotonic_ms(uint64_t *out) { *out=now++;return 0; }
int x86os_sleep_ms(uint32_t ms) { now+=ms;return 0; }
int x86os_process_identity_of(int pid,x86os_process_identity_t *out) {
    *out=(x86os_process_identity_t){1,sizeof(*out),pid,owner_generation};return 0;
}
/* RING3_PRODUCTION */
int main(void) {
    svga2d_driver_t driver={0};driver.bootstrap.device=66;
    reist_svga2d_message_t request={.version=1,.struct_size=64,.request_id=1,
        .operation=REIST_SVGA2D_ACTIVATE_MODE,.width=1280,.height=720},reply;
    request.reserved[1]=1;assert(handle_request(&driver,&request,&reply)==-84 && !commands);
    request.reserved[1]=0;request.source_x=1;assert(handle_request(&driver,&request,&reply)==-84 && !commands);
    request.source_x=0;assert(!handle_request(&driver,&request,&reply) && driver.active && driver.width==1280);
    assert(commands==1 && last_command==X86OS_DISPLAY_DRIVER_ACTIVATE_MODE);
    assert(!handle_request(&driver,&request,&reply) && commands==1);
    request.width=800;request.height=600;assert(handle_request(&driver,&request,&reply)==-16 && commands==1);
    assert(!deactivate(&driver));readback_error=1;
    assert(handle_request(&driver,&request,&reply)==-84 && !driver.active && kills==2);
    readback_error=0;driver_error=-5;assert(handle_request(&driver,&request,&reply)==-5 && !driver.active);
    driver_error=0;request.operation=REIST_SVGA2D_ACTIVATE;request.width=request.height=0;
    assert(!handle_request(&driver,&request,&reply) && driver.width==1024 && driver.height==768);
    static desktop_surface_manager_t manager;
    desktop_surface_runtime_t runtime={0};
    desktop_surface_runtime_client_t *client=&runtime.clients[0];
    client->active=DESKTOP_SURFACE_RUNTIME_BOUND;client->owner=(reist_gui_surface_owner_t){42,7};
    manager.slots[0].active=1;manager.slots[0].owner=client->owner;
    manager.slots[0].handle=(reist_gui_surface_handle_t){1,3};
    reist_gui_surface_message_t launch={.protocol_version=REIST_GUI_SURFACE_PROTOCOL_VERSION,
        .message_size=sizeof(launch),.type=REIST_GUI_SURFACE_OPEN_DISPLAY,.surface={1,3}},response;
    assert(queue_display_applet(client,&manager,&launch,&response)==-13);
    assert(!desktop_surface_runtime_allow_display(&runtime,42));
    launch.flags=1;assert(queue_display_applet(client,&manager,&launch,&response)==-22);
    launch.flags=0;launch.surface.generation=2;assert(queue_display_applet(client,&manager,&launch,&response)==-3);
    launch.surface.generation=3;assert(!queue_display_applet(client,&manager,&launch,&response));
    assert(!queue_display_applet(client,&manager,&launch,&response));
    assert(desktop_surface_runtime_take_display(&runtime)==1 && !desktop_surface_runtime_take_display(&runtime));
    assert(!queue_display_applet(client,&manager,&launch,&response));owner_generation=8;
    assert(!desktop_surface_runtime_take_display(&runtime));owner_generation=7;
    assert(!queue_display_applet(client,&manager,&launch,&response));client->active=DESKTOP_SURFACE_RUNTIME_RETIRING;
    assert(!desktop_surface_runtime_take_display(&runtime));
    reist_display_mode_request_t mode={.version=1,.struct_size=64,.operation=REIST_DISPLAY_MODE_QUERY};
    memory_error=1;assert(syscall_display_mode(&mode)==-14 && !queries && !mode_activations);
    memory_error=0;mode.reserved2=1;assert(syscall_display_mode(&mode)==-22 && !queries);
    mode.reserved2=0;assert(!syscall_display_mode(&mode) && queries==1 && !mode_activations);
    mode=(reist_display_mode_request_t){.version=1,.struct_size=64,.operation=REIST_DISPLAY_MODE_ACTIVATE,
        .width=800,.height=600,.bpp=32};
    strcpy(process.image_path,"/usr/gui/bin/display.prg");assert(syscall_display_mode(&mode)==-13 && !mode_activations);
    strcpy(process.image_path,"/usr/gui/bin/desktop.prg");mode.bpp=16;
    assert(syscall_display_mode(&mode)==-22 && !mode_activations);
    mode.bpp=32;assert(!syscall_display_mode(&mode) && mode_activations==1);
    puts("DISPLAY_TEST_OK");return 0;
}
#endif
