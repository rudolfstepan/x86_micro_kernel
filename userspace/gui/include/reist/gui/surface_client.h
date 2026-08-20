/**
 * @file reist/gui/surface_client.h
 * @brief Ring-3-Clientwrapper für den versionierten Surface-Vertrag.
 *
 * Der Aufrufer besitzt den bereits delegierten Compositor-IPC-Handle. Die
 * Bibliothek führt keine Display-Syscalls aus und kennt keine globalen
 * Koordinaten. IPC-Nachrichten bleiben synchron und kapazitätsbegrenzt.
 */
#ifndef REIST_GUI_SURFACE_CLIENT_H
#define REIST_GUI_SURFACE_CLIENT_H

#include <stdint.h>
#include "x86os.h"
#include "reist/gui/surface.h"

typedef struct reist_gui_surface_client {
    x86os_ipc_handle_t endpoint;
    reist_gui_surface_handle_t surface;
    uint32_t configured_serial;
    uint32_t acknowledged_serial;
    uint32_t width;
    uint32_t height;
    uint32_t connected;
} reist_gui_surface_client_t;

/** Initialize a client around a capability delegated by the compositor. */
int reist_gui_surface_client_init(reist_gui_surface_client_t *client,
                                  x86os_ipc_handle_t endpoint);
/** Parse the bounded compositor argument: --reist-surface=<handle>. */
int reist_gui_surface_endpoint_from_argv(int argc, char **argv,
                                         x86os_ipc_handle_t *endpoint);
/** Validate buffer metadata without accessing or mapping the buffer. */
int reist_gui_surface_buffer_validate(const reist_gui_surface_buffer_t *buffer);
/** Create a toplevel and synchronously receive its initial configure. */
int reist_gui_surface_client_create(reist_gui_surface_client_t *client,
                                    uint32_t role, uint32_t width,
                                    uint32_t height);
int reist_gui_surface_client_ack_configure(reist_gui_surface_client_t *client,
                                           uint32_t serial);
int reist_gui_surface_client_buffer_create(
    reist_gui_surface_client_t *client,
    const reist_gui_surface_buffer_t *buffer);
int reist_gui_surface_client_buffer_destroy(
    reist_gui_surface_client_t *client, uint32_t capability_id,
    uint32_t capability_generation);
int reist_gui_surface_client_attach(reist_gui_surface_client_t *client,
                                    uint32_t buffer_id,
                                    uint32_t buffer_generation);
int reist_gui_surface_client_damage(reist_gui_surface_client_t *client,
                                    reist_gui_rect_t damage);
int reist_gui_surface_client_commit(reist_gui_surface_client_t *client);
/** Receive one compositor event; timeout zero performs a nonblocking poll. */
int reist_gui_surface_client_receive(reist_gui_surface_client_t *client,
                                     reist_gui_surface_message_t *message,
                                     uint32_t timeout_ms);
int reist_gui_surface_client_receive_input(
    reist_gui_surface_client_t *client, reist_gui_surface_input_t *event,
    uint32_t timeout_ms);
int reist_gui_surface_client_destroy(reist_gui_surface_client_t *client);

#endif
