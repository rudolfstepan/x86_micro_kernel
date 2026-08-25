/**
 * @file nvidia_gk208.c
 * @brief Supervised Ring-3 policy driver for native GK208 2D bring-up.
 *
 * The first hardware gate is intentionally passive.  Ring 0 returns a fixed
 * read-only identity/status snapshot; this process owns the endpoint,
 * lifecycle and bounded request policy.  No acceleration capability is
 * published until a later GPFIFO self-test completes a real GPU fence.
 */
#include <stddef.h>
#include <stdint.h>

#include "x86os.h"
#include "../../video/include/reist/svga2d.h"

#define NVIDIA_DRIVER_PROBE 6U
#define NVIDIA_HEARTBEAT_MS 500U
#define NVIDIA_IPC_TIMEOUT_MS 20U
#define NVIDIA_REPLY_TIMEOUT_MS 100U
#define NVIDIA_DIAGNOSTIC_PROBE 0x4E560000U

typedef struct {
    x86os_device_driver_bootstrap_t bootstrap;
    x86os_ipc_handle_t control;
    uint32_t width;
    uint32_t height;
    uint32_t active;
    uint32_t progress;
} nvidia_driver_t;

static void bytes_zero(void *memory, size_t length) {
    uint8_t *bytes = memory;
    for (size_t index = 0U; index < length; ++index) bytes[index] = 0U;
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

static int probe(nvidia_driver_t *driver) {
    x86os_display_driver_request_t response;
    int status = driver_command(driver, NVIDIA_DRIVER_PROBE, &response);
    if (status != 0 || response.capabilities != 0U ||
        response.source_x == 0U || response.source_x == UINT32_MAX ||
        response.width < 0x00400104U)
        return status != 0 ? status : -84;
    /* Low 16 bits are diagnostic evidence only, never capability state. */
    return x86os_device_driver_report(
        &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_DIAGNOSTIC,
        NVIDIA_DIAGNOSTIC_PROBE | (response.source_x & 0x0000FFFFU));
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
    if (x86os_ipc_create(&driver->control) != 0) return -5;
    if (x86os_device_driver_report(
            &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_CHANNEL,
            driver->control) != 0)
        return -5;
    int status = probe(driver);
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
