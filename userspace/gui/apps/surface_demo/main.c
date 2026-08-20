/** @file main.c @brief First isolated windowed REIST Surface client. */
#include "x86os.h"
#include "reist/gui/surface_client.h"

static void print_status(int status) {
    char digits[11];
    uint32_t count = 0U;
    uint32_t magnitude;
    if (status < 0) {
        x86os_putchar('-');
        magnitude = (uint32_t)(-(int64_t)status);
    } else magnitude = (uint32_t)status;
    do {
        digits[count++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U && count < sizeof(digits));
    while (count != 0U) x86os_putchar(digits[--count]);
}

int main(int argc, char **argv) {
    x86os_ipc_handle_t endpoint = 0U;
    if (reist_gui_surface_endpoint_from_argv(argc, argv, &endpoint) != 0) {
        x86os_puts("surface-demo: compositor endpoint missing\n");
        return 2;
    }
    reist_gui_surface_client_t client;
    if (reist_gui_surface_client_init(&client, endpoint) != 0)
        return 1;
    int create_status = -9;
    for (uint32_t attempt = 0U; attempt < 250U; ++attempt) {
        create_status = reist_gui_surface_client_create(
            &client, REIST_GUI_SURFACE_ROLE_TOPLEVEL, 420U, 240U);
        if (create_status == 0) break;
        /* The child can run between spawn and capability delegation. Retry
         * only while the endpoint is absent or not authorized yet. */
        if (create_status != -9 && create_status != -13) break;
        (void)x86os_sleep_ms(1U);
    }
    if (create_status != 0) {
        x86os_puts("SURFACE_DEMO_FAIL create status=");
        print_status(create_status);
        x86os_putchar('\n');
        return 1;
    }
    int ack_status = reist_gui_surface_client_ack_configure(
        &client, client.configured_serial);
    if (ack_status != 0) {
        x86os_puts("SURFACE_DEMO_FAIL ack status=");
        print_status(ack_status);
        x86os_putchar('\n');
        return 1;
    }

    for (;;) {
        reist_gui_surface_message_t message;
        int status = reist_gui_surface_client_receive(
            &client, &message, 250U);
        if (status == 0 && message.type == REIST_GUI_SURFACE_CLOSE) break;
        if (status == 0 && message.type == REIST_GUI_SURFACE_INPUT &&
            message.input.type == REIST_GUI_SURFACE_INPUT_KEYBOARD &&
            message.input.key == 27U) break;
        if (status != 0 && status != -11) (void)x86os_sleep_ms(1U);
    }
    (void)reist_gui_surface_client_destroy(&client);
    (void)x86os_ipc_release(endpoint);
    return 0;
}
