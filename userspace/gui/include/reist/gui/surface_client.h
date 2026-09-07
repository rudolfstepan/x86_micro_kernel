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
    reist_gui_surface_message_t deferred[
        REIST_GUI_SURFACE_MAX_PENDING_EVENTS];
    uint32_t deferred_head;
    uint32_t deferred_count;
    struct reist_gui_surface_client *event_owner;
} reist_gui_surface_client_t;

/** Initialize a client around a capability delegated by the compositor. */
int reist_gui_surface_client_init(reist_gui_surface_client_t *client,
                                  x86os_ipc_handle_t endpoint);
/** Initialize another Surface on the same endpoint and shared event queue. */
int reist_gui_surface_client_init_shared(
    reist_gui_surface_client_t *client,
    reist_gui_surface_client_t *event_owner);
/** Parse the bounded compositor argument: --reist-surface=<handle>. */
int reist_gui_surface_endpoint_from_argv(int argc, char **argv,
                                         x86os_ipc_handle_t *endpoint);
/** Validate buffer metadata without accessing or mapping the buffer. */
int reist_gui_surface_buffer_validate(const reist_gui_surface_buffer_t *buffer);
/** Create a toplevel and synchronously receive its initial configure. */
int reist_gui_surface_client_create(reist_gui_surface_client_t *client,
                                    uint32_t role, uint32_t width,
                                    uint32_t height);
/** Create a transient dialog Surface bound to one live top-level parent. */
int reist_gui_surface_client_create_dialog(
    reist_gui_surface_client_t *client,
    reist_gui_surface_handle_t parent, uint32_t width, uint32_t height);
int reist_gui_surface_client_ack_configure(reist_gui_surface_client_t *client,
                                           uint32_t serial);
/** Validate, adopt and acknowledge a later compositor resize configure. */
int reist_gui_surface_client_accept_configure(
    reist_gui_surface_client_t *client,
    const reist_gui_surface_message_t *configure);
/** Set the bounded server-decoration title for this toplevel. */
int reist_gui_surface_client_set_title(reist_gui_surface_client_t *client,
                                       const char *title);
int reist_gui_surface_client_open_display(reist_gui_surface_client_t *client);
/** Start an atomic retained paint frame, replacing no visible content yet. */
int reist_gui_surface_client_paint_begin(reist_gui_surface_client_t *client);
/** Start an atomic frame for BASE or the bounded later-rendered OVERLAY. */
int reist_gui_surface_client_paint_begin_layer(
    reist_gui_surface_client_t *client, uint32_t layer);
/** Append one clipped client-local solid rectangle to the pending frame. */
int reist_gui_surface_client_paint_fill(reist_gui_surface_client_t *client,
                                        reist_gui_rect_t rect,
                                        uint32_t color);
/** Append one bounded client-local text run to the pending frame. */
int reist_gui_surface_client_paint_text(reist_gui_surface_client_t *client,
                                        int32_t x, int32_t y,
                                        uint32_t maximum_width,
                                        const char *text, uint32_t length,
                                        uint32_t foreground,
                                        uint32_t background);
/** Append text using one validated fixed catalog family and pixel height. */
int reist_gui_surface_client_paint_font_text(
    reist_gui_surface_client_t *client, int32_t x, int32_t y,
    uint32_t maximum_width, const char *text, uint32_t length,
    uint32_t foreground, uint32_t background,
    uint32_t font_family, uint32_t pixel_height);
/** Atomically publish the complete pending paint frame. */
int reist_gui_surface_client_paint_commit(reist_gui_surface_client_t *client);
/** Publish only the selected layer; the other committed layer is preserved. */
int reist_gui_surface_client_paint_commit_layer(
    reist_gui_surface_client_t *client, uint32_t layer);
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
/** Commit and return the previously committed buffer once the compositor no
 * longer references it. Zero/zero means that no older buffer was released. */
int reist_gui_surface_client_commit_with_release(
    reist_gui_surface_client_t *client, uint32_t *released_buffer_id,
    uint32_t *released_buffer_generation);
/** Receive one compositor event; timeout zero performs a nonblocking poll. */
int reist_gui_surface_client_receive(reist_gui_surface_client_t *client,
                                     reist_gui_surface_message_t *message,
                                     uint32_t timeout_ms);
int reist_gui_surface_client_receive_input(
    reist_gui_surface_client_t *client, reist_gui_surface_input_t *event,
    uint32_t timeout_ms);
int reist_gui_surface_client_destroy(reist_gui_surface_client_t *client);
/** Opt in to scroll extension v1; legacy clients remain unchanged. */
int reist_gui_surface_client_enable_scroll(reist_gui_surface_client_t *client);

#endif
