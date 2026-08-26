/**
 * @file nvidia_gk208.c
 * @brief Supervised Ring-3 policy driver for native GK208 2D bring-up.
 *
 * Ring 3 reads a fixed read-only BAR0 aperture through generation-scoped
 * device-domain mediation and owns the endpoint, lifecycle and bounded probe
 * policy.  The
 * FERMI_TWOD_A compiler is exercised in software, but no acceleration
 * capability is published until a later GPFIFO self-test completes a real
 * GPU fence.
 */
#include <stddef.h>
#include <stdint.h>

#include "x86os.h"
#include "../../video/include/reist/svga2d.h"
#include "../../video/include/reist/nvidia_gk208_2d.h"

#define NVIDIA_HEARTBEAT_MS 500U
#define NVIDIA_IPC_TIMEOUT_MS 20U
#define NVIDIA_REPLY_TIMEOUT_MS 100U
#define NVIDIA_PREFLIGHT_DELAY_MS 1U
#define NVIDIA_DIAGNOSTIC_PROBE 0x4E560000U
#define NVIDIA_DIAGNOSTIC_PREFLIGHT 0x4E570000U
#define NVIDIA_DIAGNOSTIC_COMMAND_CONTRACT 0x4E580000U
#define NVIDIA_DIAGNOSTIC_DMA_STAGING 0x4E590000U
#define NVIDIA_DIAGNOSTIC_CHANNEL_IMAGE 0x4E5A0000U
#define NVIDIA_DIAGNOSTIC_GPU_VM_PLAN 0x4E5B0000U
#define NVIDIA_DIAGNOSTIC_DMA_SEALED 0x4E5C0000U
#define NVIDIA_DIAGNOSTIC_VM_PAGE_MODE 0x4E5D0000U
#define NVIDIA_PMC_BOOT_0 0x000000U
#define NVIDIA_PMC_ENABLE 0x000200U
#define NVIDIA_PFIFO_INTR 0x002100U
#define NVIDIA_PTIMER_TIME_0 0x009400U
#define NVIDIA_PTIMER_TIME_1 0x009410U
#define NVIDIA_PGRAPH_INTR 0x400100U
#define NVIDIA_BAR0_READABLE_BYTES (NVIDIA_PGRAPH_INTR + sizeof(uint32_t))
#define NVIDIA_PROBE_COHERENCE_ATTEMPTS 4U

typedef struct {
    x86os_device_driver_bootstrap_t bootstrap;
    x86os_ipc_handle_t control;
    x86os_device_resource_t registers;
    x86os_device_resource_t dma;
    uint32_t register_bytes;
    uint32_t width;
    uint32_t height;
    uint32_t active;
    uint32_t progress;
} nvidia_driver_t;

typedef struct {
    uint32_t boot0;
    uint32_t enable;
    uint32_t timer_low;
    uint32_t timer_high;
    uint32_t pfifo_intr;
    uint32_t pgraph_intr;
} nvidia_probe_snapshot_t;

static void bytes_zero(void *memory, size_t length) {
    uint8_t *bytes = memory;
    for (size_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static int bytes_equal(const void *first, const void *second, size_t length) {
    const uint8_t *left = first;
    const uint8_t *right = second;
    for (size_t index = 0U; index < length; ++index)
        if (left[index] != right[index]) return 0;
    return 1;
}

static int driver_command(nvidia_driver_t *driver, uint32_t command,
                          x86os_display_driver_request_t *reply) {
    x86os_display_driver_request_t request;
    bytes_zero(&request, sizeof(request));
    request.version = X86OS_DISPLAY_CONTROL_VERSION;
    request.struct_size = sizeof(request);
    request.operation = X86OS_DISPLAY_DRIVER_COMMAND;
    request.device = driver->bootstrap.device;
    request.command = command;
    int status = x86os_display_driver_command(&request);
    if (reply != NULL) *reply = request;
    return status;
}

static int open_register_aperture(nvidia_driver_t *driver) {
    x86os_device_region_info_t region;
    bytes_zero(&region, sizeof(region));
    const uint32_t rights = X86OS_DEVICE_REGION_DESCRIBE |
        X86OS_DEVICE_REGION_ACCESS_READ;
    int status = x86os_device_open_region(
        driver->bootstrap.device, 0U, rights, &region);
    if (status != 0) return status;
    if (region.version != X86OS_DEVICE_ABI_VERSION ||
        region.struct_size != sizeof(region) || region.resource == 0U ||
        region.region_index != 0U || region.rights != rights ||
        (region.flags & X86OS_DEVICE_REGION_MMIO) == 0U ||
        (region.flags & X86OS_DEVICE_REGION_PIO) != 0U ||
        region.length_high != 0U ||
        region.length_low != NVIDIA_BAR0_READABLE_BYTES ||
        region.reserved[0] != 0U || region.reserved[1] != 0U)
        return -84;
    driver->registers = region.resource;
    driver->register_bytes = region.length_low;
    return 0;
}

static int register_read32(const nvidia_driver_t *driver, uint32_t offset,
                           uint32_t *value) {
    if (driver == NULL || value == NULL || driver->registers == 0U ||
        offset > driver->register_bytes ||
        sizeof(uint32_t) > driver->register_bytes - offset)
        return -22;
    return x86os_device_region_read(
        driver->registers, offset, sizeof(uint32_t), value);
}

static int read_coherent_timer(const nvidia_driver_t *driver,
                               uint32_t *low_out, uint32_t *high_out) {
    if (low_out == NULL || high_out == NULL) return -22;
    for (uint32_t attempt = 0U;
         attempt < NVIDIA_PROBE_COHERENCE_ATTEMPTS; ++attempt) {
        uint32_t high_before = 0U;
        uint32_t low = 0U;
        uint32_t high_after = 0U;
        int status = register_read32(
            driver, NVIDIA_PTIMER_TIME_1, &high_before);
        if (status != 0) return status;
        status = register_read32(driver, NVIDIA_PTIMER_TIME_0, &low);
        if (status != 0) return status;
        status = register_read32(driver, NVIDIA_PTIMER_TIME_1, &high_after);
        if (status != 0) return status;
        if (high_before == high_after) {
            *low_out = low;
            *high_out = high_after;
            return 0;
        }
    }
    return -110;
}

static int read_probe_snapshot(const nvidia_driver_t *driver,
                               nvidia_probe_snapshot_t *snapshot) {
    if (snapshot == NULL) return -22;
    bytes_zero(snapshot, sizeof(*snapshot));
    int status = register_read32(driver, NVIDIA_PMC_BOOT_0, &snapshot->boot0);
    if (status == 0)
        status = register_read32(
            driver, NVIDIA_PMC_ENABLE, &snapshot->enable);
    if (status == 0)
        status = read_coherent_timer(
            driver, &snapshot->timer_low, &snapshot->timer_high);
    if (status == 0)
        status = register_read32(
            driver, NVIDIA_PFIFO_INTR, &snapshot->pfifo_intr);
    if (status == 0)
        status = register_read32(
            driver, NVIDIA_PGRAPH_INTR, &snapshot->pgraph_intr);
    if (status != 0) return status;
    if (snapshot->boot0 == 0U || snapshot->boot0 == UINT32_MAX ||
        snapshot->enable == UINT32_MAX ||
        snapshot->timer_low == UINT32_MAX ||
        snapshot->timer_high == UINT32_MAX ||
        snapshot->pfifo_intr == UINT32_MAX ||
        snapshot->pgraph_intr == UINT32_MAX)
        return -84;
    return 0;
}

static int probe(nvidia_driver_t *driver) {
    nvidia_probe_snapshot_t snapshot;
    int status = read_probe_snapshot(driver, &snapshot);
    if (status != 0) return status;
    /* Low 16 bits are diagnostic evidence only, never capability state. */
    return x86os_device_driver_report(
        &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_DIAGNOSTIC,
        NVIDIA_DIAGNOSTIC_PROBE | (snapshot.boot0 & 0x0000FFFFU));
}

static uint64_t nvidia_gk208_timer_value(
    const nvidia_probe_snapshot_t *snapshot) {
    return ((uint64_t)snapshot->timer_high << 32U) | snapshot->timer_low;
}

static int nvidia_gk208_timer_after(
    const nvidia_probe_snapshot_t *later,
    const nvidia_probe_snapshot_t *earlier) {
    return nvidia_gk208_timer_value(later) >
           nvidia_gk208_timer_value(earlier);
}

static int engine_preflight(nvidia_driver_t *driver) {
    nvidia_probe_snapshot_t first;
    nvidia_probe_snapshot_t second;
    int status = read_probe_snapshot(driver, &first);
    if (status != 0) return status;
    if (x86os_sleep_ms(NVIDIA_PREFLIGHT_DELAY_MS) != 0) return -5;
    status = read_probe_snapshot(driver, &second);
    if (status != 0) return status;
    if (first.boot0 != second.boot0 || first.enable != second.enable ||
        !nvidia_gk208_timer_after(&second, &first))
        return -84;
    return x86os_device_driver_report(
        &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_DIAGNOSTIC,
        NVIDIA_DIAGNOSTIC_PREFLIGHT |
            (uint32_t)(nvidia_gk208_timer_value(&second) & 0xFFFFU));
}

static int command_contract_self_test(nvidia_driver_t *driver) {
    int status = reist_nvidia_gk208_command_self_test();
    if (status == 0) status = reist_nvidia_gk208_submission_self_test();
    if (status == 0) status = reist_nvidia_gk208_dma_staging_self_test();
    if (status == 0) status = reist_nvidia_gk208_channel_image_self_test();
    if (status == 0) status = reist_nvidia_gk208_vm_plan_self_test();
    if (status != 0) return status;
    return x86os_device_driver_report(
        &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_DIAGNOSTIC,
        NVIDIA_DIAGNOSTIC_COMMAND_CONTRACT |
            REIST_NVIDIA_GK208_FERMI_TWOD_A);
}

static int open_dma_pool(nvidia_driver_t *driver) {
    x86os_device_resource_result_t resource;
    bytes_zero(&resource, sizeof(resource));
    const uint32_t direction = X86OS_DEVICE_DMA_TO_DEVICE |
        X86OS_DEVICE_DMA_FROM_DEVICE;
    int status = x86os_device_bind_dma_direction(
        driver->bootstrap.device, 0U, direction, &resource);
    if (status != 0) return status;
    if (resource.version != X86OS_DEVICE_ABI_VERSION ||
        resource.struct_size != sizeof(resource) || resource.resource == 0U ||
        resource.kind != X86OS_DEVICE_RESOURCE_DMA ||
        resource.reserved[0] != 0U || resource.reserved[1] != 0U ||
        resource.reserved[2] != 0U || resource.reserved[3] != 0U)
        return -84;
    x86os_device_dma_info_t info;
    bytes_zero(&info, sizeof(info));
    status = x86os_device_dma_info(resource.resource, &info);
    if (status != 0) return status;
    if (info.version != X86OS_DEVICE_ABI_VERSION ||
        info.struct_size != sizeof(info) || info.resource != resource.resource ||
        info.capacity != REIST_NVIDIA_GK208_DMA_POOL_BYTES ||
        info.alignment != X86OS_DEVICE_DMA_DATA_OFFSET ||
        info.direction != direction || info.reserved[0] != 0U ||
        info.reserved[1] != 0U)
        return -84;
    driver->dma = resource.resource;
    return 0;
}

static int dma_staging_self_test(nvidia_driver_t *driver) {
    reist_nvidia_gk208_pushbuf_t commands;
    reist_nvidia_gk208_submission_t submission;
    reist_nvidia_gk208_dma_staging_t staging;
    const reist_nvidia_gk208_surface_t surface = {
        .gpu_address = 0x10000000ULL,
        .width = 1024U,
        .height = 768U,
        .pitch = 4096U,
    };
    const reist_nvidia_gk208_rect_t rect = {8U, 8U, 16U, 16U};
    const uint32_t fence_sequence = 1U;
    int status = reist_nvidia_gk208_encode_fill(
        &commands, &surface, &rect, 0x00010203U);
    if (status == 0)
        status = reist_nvidia_gk208_prepare_submission(
            &submission, &commands,
            REIST_NVIDIA_GK208_PUSHBUF_GPU_ADDRESS,
            REIST_NVIDIA_GK208_FENCE_GPU_ADDRESS, fence_sequence);
    if (status == 0)
        status = reist_nvidia_gk208_prepare_dma_staging(
            &staging, &submission, fence_sequence);
    if (status != 0) return status;

    const uint32_t zero_fence = 0U;
    status = x86os_device_dma_write(driver->dma, staging.gpfifo_offset,
        submission.gpfifo_entry, staging.gpfifo_bytes);
    if (status == 0)
        status = x86os_device_dma_write(driver->dma, staging.pushbuf_offset,
            submission.words, staging.pushbuf_bytes);
    if (status == 0)
        status = x86os_device_dma_write(driver->dma, staging.fence_offset,
            &zero_fence, staging.fence_bytes);
    if (status != 0) return status;

    uint32_t gpfifo_readback[REIST_NVIDIA_GK208_GPFIFO_ENTRY_WORDS];
    uint32_t pushbuf_readback[REIST_NVIDIA_GK208_SUBMISSION_WORD_CAPACITY];
    uint32_t fence_readback = UINT32_MAX;
    bytes_zero(gpfifo_readback, sizeof(gpfifo_readback));
    bytes_zero(pushbuf_readback, sizeof(pushbuf_readback));
    status = x86os_device_dma_read(driver->dma, staging.gpfifo_offset,
        gpfifo_readback, staging.gpfifo_bytes);
    if (status == 0)
        status = x86os_device_dma_read(driver->dma, staging.pushbuf_offset,
            pushbuf_readback, staging.pushbuf_bytes);
    if (status == 0)
        status = x86os_device_dma_read(driver->dma, staging.fence_offset,
            &fence_readback, staging.fence_bytes);
    if (status != 0 || !bytes_equal(gpfifo_readback,
            submission.gpfifo_entry, sizeof(gpfifo_readback)) ||
        !bytes_equal(pushbuf_readback, submission.words,
            sizeof(pushbuf_readback)) || fence_readback != zero_fence)
        return status != 0 ? status : -84;
    return x86os_device_driver_report(
        &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_DIAGNOSTIC,
        NVIDIA_DIAGNOSTIC_DMA_STAGING | submission.word_count);
}

static int dma_stage_and_verify(nvidia_driver_t *driver, uint32_t offset,
                                const void *data, uint32_t length) {
    uint8_t readback[X86OS_DEVICE_DMA_TRANSFER_MAX];
    const uint8_t *source = (const uint8_t *)data;
    uint32_t transferred = 0U;
    while (transferred < length) {
        uint32_t chunk = length - transferred;
        if (chunk > sizeof(readback)) chunk = sizeof(readback);
        bytes_zero(readback, sizeof(readback));
        int status = x86os_device_dma_write(
            driver->dma, offset + transferred, source + transferred, chunk);
        if (status == 0)
            status = x86os_device_dma_read(
                driver->dma, offset + transferred, readback, chunk);
        if (status != 0 || !bytes_equal(
                readback, source + transferred, chunk))
            return status != 0 ? status : -84;
        transferred += chunk;
    }
    return 0;
}

static int channel_image_dma_self_test(nvidia_driver_t *driver) {
    reist_nvidia_gk208_channel_image_t image;
    int status = reist_nvidia_gk208_prepare_channel_image(&image);
    if (status != 0) return status;
    status = dma_stage_and_verify(driver, image.userd_pool_offset,
        image.userd, image.userd_bytes);
    if (status == 0)
        status = dma_stage_and_verify(driver, image.ramfc_pool_offset,
            image.ramfc, image.ramfc_bytes);
    if (status == 0)
        status = dma_stage_and_verify(driver, image.runlist_pool_offset,
            image.runlist, image.runlist_bytes);
    if (status != 0 ||
        reist_nvidia_gk208_validate_channel_image(&image) != 0)
        return status != 0 ? status : -84;
    return x86os_device_driver_report(
        &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_DIAGNOSTIC,
        NVIDIA_DIAGNOSTIC_CHANNEL_IMAGE | image.channel_id);
}

static int gpu_vm_plan_dma_self_test(nvidia_driver_t *driver) {
    static const uint32_t variants[] = {
        REIST_NVIDIA_GK208_FB_PAGE_SHIFT_64K,
        REIST_NVIDIA_GK208_FB_PAGE_SHIFT_128K,
    };
    for (uint32_t variant = 0U;
         variant < sizeof(variants) / sizeof(variants[0]); ++variant) {
        reist_nvidia_gk208_vm_plan_t plan;
        int status = reist_nvidia_gk208_prepare_vm_plan(
            &plan, variants[variant]);
        if (status != 0) return status;
        for (uint32_t index = 0U; index < plan.relocation_count; ++index) {
            uint64_t unresolved_address = UINT64_MAX;
            status = x86os_device_dma_read(driver->dma,
                plan.relocations[index].destination_pool_offset,
                &unresolved_address, sizeof(unresolved_address));
            if (status != 0 || unresolved_address != 0U)
                return status != 0 ? status : -84;
        }
    }
    const uint64_t vm_limit = REIST_NVIDIA_GK208_VM_LIMIT - 1U;
    int status = dma_stage_and_verify(driver,
        REIST_NVIDIA_GK208_DMA_RAMFC_OFFSET +
            REIST_NVIDIA_GK208_RAMFC_VM_LIMIT_OFFSET,
        &vm_limit, sizeof(vm_limit));
    if (status != 0) return status;
    return x86os_device_driver_report(
        &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_DIAGNOSTIC,
        NVIDIA_DIAGNOSTIC_GPU_VM_PLAN |
            (REIST_NVIDIA_GK208_FB_PAGE_SHIFT_64K << 8U) |
            REIST_NVIDIA_GK208_FB_PAGE_SHIFT_128K);
}

static int gpu_vm_relocate_and_seal(nvidia_driver_t *driver) {
    reist_nvidia_gk208_vm_plan_t plan;
    int status = reist_nvidia_gk208_prepare_vm_plan(
        &plan, REIST_NVIDIA_GK208_DEFAULT_FB_PAGE_SHIFT);
    if (status != 0) return status;
    x86os_device_dma_relocation_rule_t
        rules[REIST_NVIDIA_GK208_SEAL_RELOCATION_COUNT] = {0};
    rules[0] = (x86os_device_dma_relocation_rule_t){
        .destination_pool_offset = REIST_NVIDIA_GK208_DMA_RAMFC_OFFSET + 0x08U,
        .source_pool_offset = REIST_NVIDIA_GK208_DMA_USERD_OFFSET,
        .width = REIST_NVIDIA_GK208_ADDRESS_RELOCATION_WIDTH,
    };
    for (uint32_t index = 0U;
         index < REIST_NVIDIA_GK208_VM_RELOCATION_COUNT; ++index) {
        rules[index + 1U] = (x86os_device_dma_relocation_rule_t){
            .destination_pool_offset =
                plan.relocations[index].destination_pool_offset,
            .source_pool_offset =
                plan.relocations[index].source_pool_offset,
            .shift_right = plan.relocations[index].shift_right,
            .width = plan.relocations[index].width,
            .fixed_bits = plan.relocations[index].fixed_bits,
        };
    }
    status = x86os_device_dma_relocate_and_seal(
        driver->dma, REIST_NVIDIA_GK208_DEFAULT_FB_PAGE_SHIFT,
        rules, REIST_NVIDIA_GK208_SEAL_RELOCATION_COUNT);
    if (status != 0) return status;
    return x86os_device_driver_report(
        &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_DIAGNOSTIC,
        NVIDIA_DIAGNOSTIC_DMA_SEALED |
            REIST_NVIDIA_GK208_DEFAULT_FB_PAGE_SHIFT);
}

static int gpu_vm_apply_page_mode(nvidia_driver_t *driver) {
    int status = x86os_device_dma_vm_page_mode(
        driver->bootstrap.device, driver->registers, driver->dma,
        REIST_NVIDIA_GK208_DEFAULT_FB_PAGE_SHIFT);
    if (status != 0) return status;
    return x86os_device_driver_report(
        &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_DIAGNOSTIC,
        NVIDIA_DIAGNOSTIC_VM_PAGE_MODE |
            REIST_NVIDIA_GK208_DEFAULT_FB_PAGE_SHIFT);
}

static int activate(nvidia_driver_t *driver) {
    x86os_display_driver_request_t response;
    int status = driver_command(
        driver, X86OS_DISPLAY_DRIVER_ACTIVATE, &response);
    if (status != 0 || response.capabilities != 0U ||
        response.width == 0U || response.height == 0U)
        return status != 0 ? status : -84;
    driver->width = response.width;
    driver->height = response.height;
    driver->active = 1U;
    return 0;
}

static int deactivate(nvidia_driver_t *driver) {
    if (driver->active == 0U) return 0;
    int status = driver_command(
        driver, X86OS_DISPLAY_DRIVER_DEACTIVATE, NULL);
    if (status == 0) driver->active = 0U;
    return status;
}

static int request_valid(const reist_svga2d_message_t *request) {
    if (request->version != REIST_SVGA2D_ABI_VERSION ||
        request->struct_size != sizeof(*request) || request->request_id == 0U ||
        request->flags != 0U || request->reserved[0] != 0U ||
        request->reserved[1] != 0U ||
        request->operation < REIST_SVGA2D_ACTIVATE ||
        request->operation > REIST_SVGA2D_INFO)
        return -84;
    return 0;
}

static void handle_request(nvidia_driver_t *driver,
                           const reist_svga2d_message_t *request,
                           reist_svga2d_message_t *response) {
    *response = *request;
    response->flags = REIST_SVGA2D_FLAG_RESPONSE;
    response->status = request_valid(request);
    if (response->status == 0) {
        if (request->operation == REIST_SVGA2D_ACTIVATE)
            response->status = driver->active != 0U ? 0 : activate(driver);
        else if (request->operation == REIST_SVGA2D_DEACTIVATE)
            response->status = deactivate(driver);
        else if (request->operation == REIST_SVGA2D_RECT_FILL ||
                 request->operation == REIST_SVGA2D_RECT_COPY)
            response->status = -95;
    }
    response->width = driver->width;
    response->height = driver->height;
    response->capabilities = 0U;
}

static int ipc_decode(const x86os_ipc_message_t *ipc,
                      reist_svga2d_message_t *wire) {
    if (ipc->version != X86OS_IPC_MESSAGE_VERSION ||
        ipc->struct_size != sizeof(*ipc) || ipc->length != sizeof(*wire))
        return -84;
    for (size_t index = 0U; index < sizeof(*wire); ++index)
        ((uint8_t *)wire)[index] = ipc->payload[index];
    return 0;
}

static void ipc_encode(x86os_ipc_message_t *ipc,
                       const reist_svga2d_message_t *wire) {
    bytes_zero(ipc, sizeof(*ipc));
    ipc->version = X86OS_IPC_MESSAGE_VERSION;
    ipc->struct_size = sizeof(*ipc);
    ipc->length = sizeof(*wire);
    for (size_t index = 0U; index < sizeof(*wire); ++index)
        ipc->payload[index] = ((const uint8_t *)wire)[index];
}

static void service_poll(nvidia_driver_t *driver) {
    x86os_ipc_message_t ipc;
    bytes_zero(&ipc, sizeof(ipc));
    ipc.version = X86OS_IPC_MESSAGE_VERSION;
    ipc.struct_size = sizeof(ipc);
    if (x86os_ipc_receive_timeout(
            driver->control, &ipc, NVIDIA_IPC_TIMEOUT_MS) != 0)
        return;
    reist_svga2d_message_t request;
    reist_svga2d_message_t response;
    if (ipc_decode(&ipc, &request) != 0) return;
    handle_request(driver, &request, &response);
    ipc_encode(&ipc, &response);
    (void)x86os_ipc_send_timeout(
        driver->control, &ipc, NVIDIA_REPLY_TIMEOUT_MS);
}

static int driver_initialize(nvidia_driver_t *driver) {
    bytes_zero(driver, sizeof(*driver));
    if (x86os_device_driver_bootstrap(&driver->bootstrap) != 0 ||
        driver->bootstrap.mode != X86OS_DEVICE_MODE_MEDIATED)
        return -13;
    int status = open_register_aperture(driver);
    if (status != 0) return status;
    if (x86os_ipc_create(&driver->control) != 0) return -5;
    if (x86os_device_driver_report(
            &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_CHANNEL,
            driver->control) != 0)
        return -5;
    status = probe(driver);
    if (status != 0) return status;
    status = engine_preflight(driver);
    if (status != 0) return status;
    status = command_contract_self_test(driver);
    if (status != 0) return status;
    status = open_dma_pool(driver);
    if (status != 0) return status;
    status = dma_staging_self_test(driver);
    if (status != 0) return status;
    status = channel_image_dma_self_test(driver);
    if (status != 0) return status;
    status = gpu_vm_plan_dma_self_test(driver);
    if (status != 0) return status;
    status = gpu_vm_relocate_and_seal(driver);
    if (status != 0) return status;
    status = gpu_vm_apply_page_mode(driver);
    if (status != 0) return status;
    if (x86os_device_driver_report(
            &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_SELF_TEST, 1U) != 0 ||
        x86os_device_driver_report(
            &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_PROGRESS, 1U) != 0)
        return -5;
    driver->progress = 2U;
    return 0;
}

int main(void) {
    nvidia_driver_t driver;
    if (driver_initialize(&driver) != 0) return 1;
    uint64_t last_report = 0U;
    (void)x86os_monotonic_ms(&last_report);
    for (;;) {
        service_poll(&driver);
        uint64_t now = 0U;
        if (x86os_monotonic_ms(&now) == 0 && now >= last_report &&
            now - last_report >= NVIDIA_HEARTBEAT_MS) {
            if (x86os_device_driver_report(
                    &driver.bootstrap, X86OS_DEVICE_DRIVER_REPORT_PROGRESS,
                    driver.progress++) != 0)
                return 2;
            if (driver.progress == 0U) driver.progress = 2U;
            last_report = now;
        }
    }
}
