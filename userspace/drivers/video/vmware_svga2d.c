/**
 * @file vmware_svga2d.c
 * @brief Supervised Ring-3 VMware SVGA-II 2D policy driver.
 *
 * The process owns lifecycle, capability policy and bounded completion waits.
 * A small Ring-0 mediator emits only validated fixed-format FIFO commands.
 */
#include <stddef.h>
#include <stdint.h>

#include "x86os.h"
#include "../../video/include/reist/svga2d.h"

#define SVGA2D_HEARTBEAT_MS 500U
#define SVGA2D_WAIT_DEADLINE_MS 50U
#define SVGA2D_IPC_TIMEOUT_MS 20U
#define SVGA2D_REPLY_TIMEOUT_MS 100U

typedef struct {
    x86os_device_driver_bootstrap_t bootstrap;
    x86os_ipc_handle_t control;
    uint32_t capabilities;
    uint32_t width;
    uint32_t height;
    uint32_t active;
    uint32_t progress;
} svga2d_driver_t;

static void bytes_zero(void *memory, size_t length) {
    uint8_t *bytes = memory;
    for (size_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static int driver_command(svga2d_driver_t *driver, uint32_t command,
                          uint32_t source_x, uint32_t source_y,
                          uint32_t destination_x, uint32_t destination_y,
                          uint32_t width, uint32_t height, uint32_t color,
                          x86os_display_driver_request_t *reply) {
    x86os_display_driver_request_t request;
    bytes_zero(&request, sizeof(request));
    request.version = X86OS_DISPLAY_CONTROL_VERSION;
    request.struct_size = sizeof(request);
    request.operation = X86OS_DISPLAY_DRIVER_COMMAND;
    request.device = driver->bootstrap.device;
    request.command = command;
    request.source_x = source_x;
    request.source_y = source_y;
    request.destination_x = destination_x;
    request.destination_y = destination_y;
    request.width = width;
    request.height = height;
    request.color = color;
    int status = x86os_display_driver_command(&request);
    if (reply != NULL) *reply = request;
    return status;
}

static int wait_idle(svga2d_driver_t *driver) {
    uint64_t started = 0U;
    uint64_t now = 0U;
    if (x86os_monotonic_ms(&started) != 0) return -5;
    for (;;) {
        x86os_display_driver_request_t status;
        int result = driver_command(
            driver, X86OS_DISPLAY_DRIVER_BUSY_QUERY,
            0U, 0U, 0U, 0U, 0U, 0U, 0U, &status);
        if (result != 0) return result;
        if (status.busy == 0U) return 0;
        if (x86os_monotonic_ms(&now) != 0 || now < started ||
            now - started >= SVGA2D_WAIT_DEADLINE_MS)
            return -110;
        if (x86os_sleep_ms(1U) != 0) return -5;
    }
}

static int activate(svga2d_driver_t *driver) {
    x86os_display_driver_request_t response;
    int status = driver_command(
        driver, X86OS_DISPLAY_DRIVER_ACTIVATE,
        0U, 0U, 0U, 0U, 0U, 0U, 0U, &response);
    if (status != 0 || response.width < 800U || response.height < 600U)
        return status != 0 ? status : -84;
    driver->capabilities = response.capabilities &
        (REIST_SVGA2D_CAP_RECT_FILL | REIST_SVGA2D_CAP_RECT_COPY);
    driver->width = response.width;
    driver->height = response.height;
    driver->active = 1U;
    return 0;
}

static int activate_mode(svga2d_driver_t *driver, uint32_t width, uint32_t height) {
    if (driver->active) return driver->width == width && driver->height == height ? 0 : -16;
    x86os_display_driver_request_t response;
    int status = driver_command(driver, X86OS_DISPLAY_DRIVER_ACTIVATE_MODE,
        0U, 0U, 0U, 0U, width, height, 0U, &response);
    if (status != 0) return status;
    if (response.width != width || response.height != height) {
        (void)driver_command(driver, X86OS_DISPLAY_DRIVER_DEACTIVATE,
            0U, 0U, 0U, 0U, 0U, 0U, 0U, NULL);
        return -84;
    }
    driver->width = width; driver->height = height;
    driver->capabilities = response.capabilities &
        (REIST_SVGA2D_CAP_RECT_FILL | REIST_SVGA2D_CAP_RECT_COPY);
    driver->active = 1U;
    return 0;
}

static int deactivate(svga2d_driver_t *driver) {
    if (driver->active == 0U) return 0;
    int status = driver_command(
        driver, X86OS_DISPLAY_DRIVER_DEACTIVATE,
        0U, 0U, 0U, 0U, 0U, 0U, 0U, NULL);
    if (status == 0) driver->active = 0U;
    return status;
}

static int submit_2d(svga2d_driver_t *driver,
                     const reist_svga2d_message_t *request) {
    uint32_t command = request->operation == REIST_SVGA2D_RECT_COPY
        ? X86OS_DISPLAY_DRIVER_RECT_COPY : X86OS_DISPLAY_DRIVER_RECT_FILL;
    uint32_t required = request->operation == REIST_SVGA2D_RECT_COPY
        ? REIST_SVGA2D_CAP_RECT_COPY : REIST_SVGA2D_CAP_RECT_FILL;
    if (driver->active == 0U) return -19;
    if ((driver->capabilities & required) == 0U) return -95;
    int status = driver_command(
        driver, command, request->source_x, request->source_y,
        request->destination_x, request->destination_y,
        request->width, request->height, request->color, NULL);
    return status == 0 ? wait_idle(driver) : status;
}

static int request_valid(const reist_svga2d_message_t *request) {
    if (request->version != REIST_SVGA2D_ABI_VERSION ||
        request->struct_size != sizeof(*request) || request->request_id == 0U ||
        request->flags != 0U || request->reserved[0] != 0U ||
        request->reserved[1] != 0U ||
        request->operation < REIST_SVGA2D_ACTIVATE ||
        request->operation > REIST_SVGA2D_ACTIVATE_MODE)
        return -84;
    if (request->operation == REIST_SVGA2D_ACTIVATE_MODE &&
        (!request->width || !request->height || request->source_x || request->source_y ||
         request->destination_x || request->destination_y || request->color ||
         request->capabilities || request->status)) return -84;
    return 0;
}

static int handle_request(svga2d_driver_t *driver,
                          const reist_svga2d_message_t *request,
                          reist_svga2d_message_t *response) {
    *response = *request;
    response->flags = REIST_SVGA2D_FLAG_RESPONSE;
    response->capabilities = driver->capabilities;
    response->status = request_valid(request);
    if (response->status != 0) return response->status;
    if (request->operation == REIST_SVGA2D_ACTIVATE) {
        response->status = driver->active != 0U ? 0 : activate(driver);
    } else if (request->operation == REIST_SVGA2D_ACTIVATE_MODE) {
        response->status = activate_mode(driver, request->width, request->height);
    } else if (request->operation == REIST_SVGA2D_DEACTIVATE) {
        response->status = deactivate(driver);
    } else if (request->operation == REIST_SVGA2D_RECT_FILL ||
               request->operation == REIST_SVGA2D_RECT_COPY) {
        response->status = submit_2d(driver, request);
    }
    response->width = driver->width;
    response->height = driver->height;
    response->capabilities = driver->capabilities;
    return response->status;
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

static void service_poll(svga2d_driver_t *driver) {
    x86os_ipc_message_t ipc;
    bytes_zero(&ipc, sizeof(ipc));
    ipc.version = X86OS_IPC_MESSAGE_VERSION;
    ipc.struct_size = sizeof(ipc);
    int received = x86os_ipc_receive_timeout(
        driver->control, &ipc, SVGA2D_IPC_TIMEOUT_MS);
    if (received != 0) return;
    reist_svga2d_message_t request;
    reist_svga2d_message_t response;
    if (ipc_decode(&ipc, &request) != 0) return;
    (void)handle_request(driver, &request, &response);
    ipc_encode(&ipc, &response);
    (void)x86os_ipc_send_timeout(
        driver->control, &ipc, SVGA2D_REPLY_TIMEOUT_MS);
}

static int driver_initialize(svga2d_driver_t *driver) {
    bytes_zero(driver, sizeof(*driver));
    if (x86os_device_driver_bootstrap(&driver->bootstrap) != 0 ||
        driver->bootstrap.mode != X86OS_DEVICE_MODE_MEDIATED)
        return -13;
    if (x86os_ipc_create(&driver->control) != 0) return -5;
    if (x86os_device_driver_report(
            &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_CHANNEL,
            driver->control) != 0)
        return -5;
    x86os_display_driver_request_t probe;
    int status = driver_command(
        driver, X86OS_DISPLAY_DRIVER_PROBE,
        0U, 0U, 0U, 0U, 0U, 0U, 0U, &probe);
    if (status == 0)
        status = driver_command(
            driver, X86OS_DISPLAY_DRIVER_ENGINE_PREFLIGHT,
            0U, 0U, 0U, 0U, 0U, 0U, 0U, &probe);
    if (status != 0 || probe.width < 800U || probe.height < 600U) {
        (void)x86os_device_driver_report(
            &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_DIAGNOSTIC,
            0x51000000U | ((uint32_t)(-status) & 0x0000FFFFU));
        return status != 0 ? status : -84;
    }
    driver->capabilities = probe.capabilities &
        (REIST_SVGA2D_CAP_RECT_FILL | REIST_SVGA2D_CAP_RECT_COPY);
    driver->width = probe.width;
    driver->height = probe.height;
    if (x86os_device_driver_report(
            &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_SELF_TEST, 1U) != 0 ||
        x86os_device_driver_report(
            &driver->bootstrap, X86OS_DEVICE_DRIVER_REPORT_PROGRESS, 1U) != 0)
        return -5;
    driver->progress = 2U;
    return 0;
}

int main(void) {
    svga2d_driver_t driver;
    if (driver_initialize(&driver) != 0) return 1;
    uint64_t last_report = 0U;
    (void)x86os_monotonic_ms(&last_report);
    for (;;) {
        service_poll(&driver);
        uint64_t now = 0U;
        if (x86os_monotonic_ms(&now) == 0 && now >= last_report &&
            now - last_report >= SVGA2D_HEARTBEAT_MS) {
            if (x86os_device_driver_report(
                    &driver.bootstrap, X86OS_DEVICE_DRIVER_REPORT_PROGRESS,
                    driver.progress++) != 0)
                return 2;
            if (driver.progress == 0U) driver.progress = 2U;
            last_report = now;
        }
    }
}
