/**
 * @file desktop_surface.h
 * @brief Server-side fixed-capacity Surface state machine.
 *
 * No drawing, IPC or allocation is performed here. The desktop owns this
 * state and serializes calls from its IPC/event loop.
 */
#ifndef USERSPACE_DESKTOP_SURFACE_H
#define USERSPACE_DESKTOP_SURFACE_H

#include <stdint.h>
#include "reist/gui/surface.h"

#define DESKTOP_SURFACE_CAPACITY REIST_GUI_SURFACE_MAX_SURFACES
#define DESKTOP_SURFACE_NO_SLOT UINT32_MAX
#define DESKTOP_SURFACE_BUFFER_CAPACITY REIST_GUI_SURFACE_MAX_SURFACES

enum desktop_surface_status {
    DESKTOP_SURFACE_OK = 0,
    DESKTOP_SURFACE_EINVAL = -22,
    DESKTOP_SURFACE_ESTALE = -116,
    DESKTOP_SURFACE_ECAPACITY = -75,
    DESKTOP_SURFACE_ESTATE = -114
};

enum desktop_surface_paint_type {
    DESKTOP_SURFACE_PAINT_NONE = 0U,
    DESKTOP_SURFACE_PAINT_FILL,
    DESKTOP_SURFACE_PAINT_TEXT
};

typedef struct desktop_surface_paint_command {
    uint32_t type;
    reist_gui_rect_t rect;
    uint32_t foreground;
    uint32_t background;
    uint32_t text_length;
    char text[REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY];
} desktop_surface_paint_command_t;

typedef struct desktop_surface_slot {
    uint32_t active;
    reist_gui_surface_owner_t owner;
    reist_gui_surface_handle_t handle;
    uint32_t role;
    reist_gui_surface_handle_t parent;
    uint32_t configured_serial;
    uint32_t acknowledged_serial;
    uint32_t configure_sent;
    uint32_t width;
    uint32_t height;
    uint32_t pending_width;
    uint32_t pending_height;
    uint32_t attached_buffer;
    uint32_t attached_generation;
    uint32_t attached;
    uint32_t committed;
    uint32_t committed_buffer;
    uint32_t committed_buffer_generation;
    uint32_t window_index;
    uint32_t close_sent;
    char title[REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY];
    desktop_surface_paint_command_t pending_paint[
        REIST_GUI_SURFACE_MAX_PAINT_COMMANDS];
    desktop_surface_paint_command_t committed_paint[
        REIST_GUI_SURFACE_MAX_PAINT_COMMANDS];
    desktop_surface_paint_command_t committed_overlay_paint[
        REIST_GUI_SURFACE_MAX_OVERLAY_PAINT_COMMANDS];
    desktop_surface_paint_command_t committed_dynamic_paint[
        REIST_GUI_SURFACE_MAX_DYNAMIC_PAINT_COMMANDS];
    desktop_surface_paint_command_t committed_hover_paint[
        REIST_GUI_SURFACE_MAX_HOVER_PAINT_COMMANDS];
    uint32_t pending_paint_count;
    uint32_t committed_paint_count;
    uint32_t committed_overlay_paint_count;
    uint32_t committed_dynamic_paint_count;
    uint32_t committed_hover_paint_count;
    uint32_t paint_active;
    uint32_t pending_paint_layer;
    uint32_t paint_generation;
    uint32_t presented_generation;
    reist_gui_rect_t present_damage;
    uint32_t present_damage_valid;
    reist_gui_surface_damage_t damage;
    reist_gui_surface_input_t pending_events[
        REIST_GUI_SURFACE_MAX_PENDING_EVENTS];
    uint32_t event_head;
    uint32_t event_count;
} desktop_surface_slot_t;

typedef struct desktop_surface_manager {
    desktop_surface_slot_t slots[DESKTOP_SURFACE_CAPACITY];
    uint32_t next_generation;
    uint32_t next_configure_serial;
    struct {
        uint32_t active;
        reist_gui_surface_owner_t owner;
        reist_gui_surface_buffer_t descriptor;
    } buffers[DESKTOP_SURFACE_BUFFER_CAPACITY];
} desktop_surface_manager_t;

typedef struct desktop_surface_commit_result {
    uint32_t committed;
    uint32_t buffer_id;
    uint32_t buffer_generation;
    uint32_t released_buffer_id;
    uint32_t released_buffer_generation;
    reist_gui_surface_damage_t damage;
} desktop_surface_commit_result_t;

void desktop_surface_initialize(desktop_surface_manager_t *manager);
int desktop_surface_create(desktop_surface_manager_t *manager,
                           reist_gui_surface_owner_t owner, uint32_t role,
                           uint32_t width, uint32_t height,
                           reist_gui_surface_handle_t *handle,
                           reist_gui_surface_configure_t *configure);
int desktop_surface_create_dialog(desktop_surface_manager_t *manager,
                                  reist_gui_surface_owner_t owner,
                                  reist_gui_surface_handle_t parent,
                                  uint32_t width, uint32_t height,
                                  reist_gui_surface_handle_t *handle,
                                  reist_gui_surface_configure_t *configure);
int desktop_surface_ack_configure(desktop_surface_manager_t *manager,
                                  reist_gui_surface_owner_t owner,
                                  reist_gui_surface_handle_t handle,
                                  uint32_t serial);
int desktop_surface_reconfigure(desktop_surface_manager_t *manager,
                                reist_gui_surface_owner_t owner,
                                reist_gui_surface_handle_t handle,
                                uint32_t width, uint32_t height,
                                reist_gui_surface_configure_t *configure);
int desktop_surface_attach(desktop_surface_manager_t *manager,
                           reist_gui_surface_owner_t owner,
                           reist_gui_surface_handle_t handle,
                           uint32_t buffer_id, uint32_t buffer_generation,
                           uint32_t width, uint32_t height);
int desktop_surface_damage(desktop_surface_manager_t *manager,
                            reist_gui_surface_owner_t owner,
                            reist_gui_surface_handle_t handle,
                            reist_gui_rect_t damage);
int desktop_surface_commit(desktop_surface_manager_t *manager,
                            reist_gui_surface_owner_t owner,
                            reist_gui_surface_handle_t handle,
                            desktop_surface_commit_result_t *result);
int desktop_surface_present_damage_take(desktop_surface_manager_t *manager,
                                        reist_gui_surface_owner_t owner,
                                        reist_gui_surface_handle_t handle,
                                        reist_gui_rect_t *damage);
int desktop_surface_destroy(desktop_surface_manager_t *manager,
                            reist_gui_surface_owner_t owner,
                            reist_gui_surface_handle_t handle);
int desktop_surface_buffer_create(desktop_surface_manager_t *manager,
                                  reist_gui_surface_owner_t owner,
                                  const reist_gui_surface_buffer_t *buffer);
int desktop_surface_buffer_destroy(desktop_surface_manager_t *manager,
                                   reist_gui_surface_owner_t owner,
                                   uint32_t capability_id,
                                   uint32_t capability_generation);
void desktop_surface_revoke_owner(desktop_surface_manager_t *manager,
                                  reist_gui_surface_owner_t owner);
int desktop_surface_input_enqueue(desktop_surface_manager_t *manager,
                                  reist_gui_surface_owner_t owner,
                                  reist_gui_surface_handle_t handle,
                                  const reist_gui_surface_input_t *event);
int desktop_surface_input_dequeue(desktop_surface_manager_t *manager,
                                  reist_gui_surface_owner_t owner,
                                  reist_gui_surface_handle_t handle,
                                  reist_gui_surface_input_t *event);
int desktop_surface_set_title(desktop_surface_manager_t *manager,
                              reist_gui_surface_owner_t owner,
                              reist_gui_surface_handle_t handle,
                              const char *title, uint32_t length);
int desktop_surface_paint_begin(desktop_surface_manager_t *manager,
                                reist_gui_surface_owner_t owner,
                                reist_gui_surface_handle_t handle);
int desktop_surface_paint_begin_layer(desktop_surface_manager_t *manager,
                                      reist_gui_surface_owner_t owner,
                                      reist_gui_surface_handle_t handle,
                                      uint32_t layer);
int desktop_surface_paint_fill(desktop_surface_manager_t *manager,
                               reist_gui_surface_owner_t owner,
                               reist_gui_surface_handle_t handle,
                               reist_gui_rect_t rect, uint32_t color);
int desktop_surface_paint_text(desktop_surface_manager_t *manager,
                               reist_gui_surface_owner_t owner,
                               reist_gui_surface_handle_t handle,
                               reist_gui_rect_t rect, uint32_t foreground,
                               uint32_t background, const char *text,
                               uint32_t length);
int desktop_surface_paint_commit(desktop_surface_manager_t *manager,
                                 reist_gui_surface_owner_t owner,
                                 reist_gui_surface_handle_t handle);
int desktop_surface_paint_commit_layer(desktop_surface_manager_t *manager,
                                       reist_gui_surface_owner_t owner,
                                       reist_gui_surface_handle_t handle,
                                       uint32_t layer);

/** Validate and apply one complete wire message without partial mutation. */
int desktop_surface_dispatch_message(
    desktop_surface_manager_t *manager,
    reist_gui_surface_owner_t owner,
    const reist_gui_surface_message_t *request,
    reist_gui_surface_message_t *response);

#endif
