/** @file desktop_surface_runtime.h @brief Desktop Surface-IPC broker. */
#ifndef USERSPACE_DESKTOP_SURFACE_RUNTIME_H
#define USERSPACE_DESKTOP_SURFACE_RUNTIME_H
#include <stdint.h>
#include "x86os.h"
#include "desktop_surface.h"
#define DESKTOP_SURFACE_RUNTIME_CAPACITY REIST_GUI_SURFACE_MAX_CLIENTS
/* One retained paint frame contains at most 192 commands. The IPC queue holds
 * four messages, therefore 64 cooperative refill rounds cover one complete
 * frame plus transaction messages without making broker work unbounded. */
#define DESKTOP_SURFACE_RUNTIME_DRAIN_ROUNDS 64U
#define DESKTOP_SURFACE_RUNTIME_RETIRE_TIMEOUT_MS 1000U
#define DESKTOP_SURFACE_RUNTIME_FREE 0U
#define DESKTOP_SURFACE_RUNTIME_BOUND 1U
#define DESKTOP_SURFACE_RUNTIME_RESERVED 2U
#define DESKTOP_SURFACE_RUNTIME_RETIRING 3U
typedef struct desktop_surface_runtime_client {
    x86os_ipc_handle_t endpoint;
    reist_gui_surface_owner_t owner;
    uint32_t active;
    uint32_t terminate_requested;
    uint64_t retire_deadline_ms;
} desktop_surface_runtime_client_t;
typedef struct desktop_surface_runtime {
    desktop_surface_runtime_client_t clients[DESKTOP_SURFACE_RUNTIME_CAPACITY];
} desktop_surface_runtime_t;
int desktop_surface_runtime_initialize(desktop_surface_runtime_t *runtime);
int desktop_surface_runtime_reserve(desktop_surface_runtime_t *runtime,
                                    x86os_ipc_handle_t *client_endpoint);
int desktop_surface_runtime_bind(desktop_surface_runtime_t *runtime,
                                 x86os_ipc_handle_t endpoint, int pid);
void desktop_surface_runtime_cancel(desktop_surface_runtime_t *runtime,
                                    x86os_ipc_handle_t endpoint);
int desktop_surface_runtime_open_for_process(desktop_surface_runtime_t *runtime,
                                             int pid,
                                             x86os_ipc_handle_t *client_endpoint);
int desktop_surface_runtime_poll(desktop_surface_runtime_t *runtime,
                                  desktop_surface_manager_t *manager);
int desktop_surface_runtime_send_close(
    desktop_surface_runtime_t *runtime,
    reist_gui_surface_owner_t owner,
    reist_gui_surface_handle_t surface);
int desktop_surface_runtime_send_configure(
    desktop_surface_runtime_t *runtime,
    reist_gui_surface_owner_t owner,
    reist_gui_surface_handle_t surface,
    const reist_gui_surface_configure_t *configure);
void desktop_surface_runtime_shutdown(desktop_surface_runtime_t *runtime);
#endif
