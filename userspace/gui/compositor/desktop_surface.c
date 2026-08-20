/** @file desktop_surface.c @brief Bounded compositor Surface state. */
#include "desktop_surface.h"

static int valid_owner(reist_gui_surface_owner_t owner) {
    return owner.pid != 0U && owner.process_generation != 0U;
}

static int find_slot(const desktop_surface_manager_t *manager,
                     reist_gui_surface_owner_t owner,
                     reist_gui_surface_handle_t handle) {
    if (!manager || !valid_owner(owner) || handle.id == 0U ||
        handle.id > DESKTOP_SURFACE_CAPACITY) return -1;
    uint32_t index = handle.id - 1U;
    const desktop_surface_slot_t *slot = &manager->slots[index];
    if (!slot->active || slot->handle.generation != handle.generation ||
        slot->owner.pid != owner.pid ||
        slot->owner.process_generation != owner.process_generation) return -1;
    return (int)index;
}

static uint32_t next_nonzero(uint32_t *value) {
    ++*value;
    if (*value == 0U) ++*value;
    return *value;
}

static int valid_local_rect(const desktop_surface_slot_t *slot,
                            reist_gui_rect_t rect) {
    return slot != 0 && rect.x >= 0 && rect.y >= 0 && rect.width != 0U &&
        rect.height != 0U && (uint32_t)rect.x < slot->width &&
        (uint32_t)rect.y < slot->height &&
        rect.width <= slot->width - (uint32_t)rect.x &&
        rect.height <= slot->height - (uint32_t)rect.y;
}

static void copy_bounded_text(char *destination, const char *source,
                              uint32_t length) {
    uint32_t index = 0U;
    for (; index < length; ++index) destination[index] = source[index];
    for (; index < REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY; ++index)
        destination[index] = '\0';
}

static int valid_buffer(const reist_gui_surface_buffer_t *buffer) {
    return buffer && buffer->version == REIST_GUI_SURFACE_BUFFER_API_VERSION &&
        buffer->struct_size == sizeof(*buffer) && buffer->capability_id != 0U &&
        buffer->capability_generation != 0U && buffer->width != 0U &&
        buffer->height != 0U && buffer->width <= REIST_GUI_SURFACE_MAX_WIDTH &&
        buffer->height <= REIST_GUI_SURFACE_MAX_HEIGHT &&
        buffer->format == REIST_GUI_SURFACE_BUFFER_FORMAT_XRGB8888 &&
        buffer->width <= UINT32_MAX / 4U &&
        buffer->stride_bytes >= buffer->width * 4U &&
        buffer->height <= UINT32_MAX / buffer->stride_bytes &&
        buffer->byte_size == buffer->height * buffer->stride_bytes &&
        buffer->byte_size <= REIST_GUI_SURFACE_MAX_BUFFER_BYTES;
}

static int find_buffer(const desktop_surface_manager_t *manager,
                       reist_gui_surface_owner_t owner, uint32_t id,
                       uint32_t generation) {
    if (!manager || !valid_owner(owner) || id == 0U || generation == 0U)
        return -1;
    for (uint32_t i = 0U; i < DESKTOP_SURFACE_BUFFER_CAPACITY; ++i) {
        if (manager->buffers[i].active &&
            manager->buffers[i].owner.pid == owner.pid &&
            manager->buffers[i].owner.process_generation ==
                owner.process_generation &&
            manager->buffers[i].descriptor.capability_id == id &&
            manager->buffers[i].descriptor.capability_generation == generation)
            return (int)i;
    }
    return -1;
}

int desktop_surface_buffer_create(desktop_surface_manager_t *manager,
                                  reist_gui_surface_owner_t owner,
                                  const reist_gui_surface_buffer_t *buffer) {
    if (!manager || !valid_owner(owner) || !valid_buffer(buffer))
        return DESKTOP_SURFACE_EINVAL;
    if (find_buffer(manager, owner, buffer->capability_id,
                    buffer->capability_generation) >= 0)
        return DESKTOP_SURFACE_ESTATE;
    for (uint32_t i = 0U; i < DESKTOP_SURFACE_BUFFER_CAPACITY; ++i) {
        if (!manager->buffers[i].active) {
            manager->buffers[i].active = 1U;
            manager->buffers[i].owner = owner;
            manager->buffers[i].descriptor = *buffer;
            return DESKTOP_SURFACE_OK;
        }
    }
    return DESKTOP_SURFACE_ECAPACITY;
}

int desktop_surface_buffer_destroy(desktop_surface_manager_t *manager,
                                   reist_gui_surface_owner_t owner,
                                   uint32_t capability_id,
                                   uint32_t capability_generation) {
    int index = find_buffer(manager, owner, capability_id,
                            capability_generation);
    if (index < 0) return DESKTOP_SURFACE_ESTALE;
    for (uint32_t i = 0U; i < DESKTOP_SURFACE_CAPACITY; ++i) {
        if (manager->slots[i].active && manager->slots[i].attached &&
            manager->slots[i].owner.pid == owner.pid &&
            manager->slots[i].owner.process_generation ==
                owner.process_generation &&
            manager->slots[i].attached_buffer == capability_id &&
            manager->slots[i].attached_generation == capability_generation)
            return DESKTOP_SURFACE_ESTATE;
        if (manager->slots[i].active && manager->slots[i].committed &&
            manager->slots[i].owner.pid == owner.pid &&
            manager->slots[i].owner.process_generation ==
                owner.process_generation &&
            manager->slots[i].committed_buffer == capability_id &&
            manager->slots[i].committed_buffer_generation ==
                capability_generation)
            return DESKTOP_SURFACE_ESTATE;
    }
    uint8_t *bytes = (uint8_t *)&manager->buffers[index];
    for (uint32_t i = 0U; i < sizeof(manager->buffers[index]); ++i)
        bytes[i] = 0U;
    return DESKTOP_SURFACE_OK;
}

void desktop_surface_revoke_owner(desktop_surface_manager_t *manager,
                                  reist_gui_surface_owner_t owner) {
    if (!manager || !valid_owner(owner)) return;
    for (uint32_t i = 0U; i < DESKTOP_SURFACE_CAPACITY; ++i) {
        if (manager->slots[i].active &&
            manager->slots[i].owner.pid == owner.pid &&
            manager->slots[i].owner.process_generation ==
                owner.process_generation) {
            uint8_t *bytes = (uint8_t *)&manager->slots[i];
            for (uint32_t n = 0U; n < sizeof(manager->slots[i]); ++n)
                bytes[n] = 0U;
        }
    }
    for (uint32_t i = 0U; i < DESKTOP_SURFACE_BUFFER_CAPACITY; ++i) {
        if (manager->buffers[i].active &&
            manager->buffers[i].owner.pid == owner.pid &&
            manager->buffers[i].owner.process_generation ==
                owner.process_generation) {
            uint8_t *bytes = (uint8_t *)&manager->buffers[i];
            for (uint32_t n = 0U; n < sizeof(manager->buffers[i]); ++n)
                bytes[n] = 0U;
        }
    }
}

void desktop_surface_initialize(desktop_surface_manager_t *manager) {
    if (!manager) return;
    uint8_t *bytes = (uint8_t *)manager;
    for (uint32_t i = 0U; i < sizeof(*manager); ++i) bytes[i] = 0U;
    manager->next_generation = 0U;
    manager->next_configure_serial = 0U;
}

int desktop_surface_create(desktop_surface_manager_t *manager,
                           reist_gui_surface_owner_t owner, uint32_t role,
                           uint32_t width, uint32_t height,
                           reist_gui_surface_handle_t *handle,
                           reist_gui_surface_configure_t *configure) {
    if (!manager || !valid_owner(owner) || role == REIST_GUI_SURFACE_ROLE_NONE ||
        !handle || !configure || width == 0U || height == 0U ||
        width > REIST_GUI_SURFACE_MAX_WIDTH ||
        height > REIST_GUI_SURFACE_MAX_HEIGHT) return DESKTOP_SURFACE_EINVAL;
    for (uint32_t index = 0U; index < DESKTOP_SURFACE_CAPACITY; ++index) {
        desktop_surface_slot_t *slot = &manager->slots[index];
        if (slot->active) continue;
        uint8_t *slot_bytes = (uint8_t *)slot;
        for (uint32_t i = 0U; i < sizeof(*slot); ++i) slot_bytes[i] = 0U;
        slot->active = 1U;
        slot->owner = owner;
        slot->handle.id = index + 1U;
        slot->handle.generation = next_nonzero(&manager->next_generation);
        slot->role = role;
        slot->width = width;
        slot->height = height;
        slot->configured_serial = next_nonzero(&manager->next_configure_serial);
        slot->acknowledged_serial = 0U;
        slot->configure_sent = 1U;
        slot->attached = 0U;
        slot->committed = 0U;
        slot->window_index = DESKTOP_SURFACE_NO_SLOT;
        slot->close_sent = 0U;
        copy_bounded_text(slot->title, "Application", 11U);
        slot->damage.count = 0U;
        slot->damage.reserved = 0U;
        *handle = slot->handle;
        *configure = (reist_gui_surface_configure_t){
            slot->configured_serial, width, height, 0U, 0U};
        return DESKTOP_SURFACE_OK;
    }
    return DESKTOP_SURFACE_ECAPACITY;
}

int desktop_surface_ack_configure(desktop_surface_manager_t *manager,
                                  reist_gui_surface_owner_t owner,
                                  reist_gui_surface_handle_t handle,
                                  uint32_t serial) {
    int index = find_slot(manager, owner, handle);
    if (index < 0 || serial == 0U ||
        serial != manager->slots[index].configured_serial)
        return DESKTOP_SURFACE_ESTALE;
    manager->slots[index].acknowledged_serial = serial;
    manager->slots[index].configure_sent = 1U;
    return DESKTOP_SURFACE_OK;
}

int desktop_surface_reconfigure(desktop_surface_manager_t *manager,
                                reist_gui_surface_owner_t owner,
                                reist_gui_surface_handle_t handle,
                                uint32_t width, uint32_t height,
                                reist_gui_surface_configure_t *configure) {
    int index = find_slot(manager, owner, handle);
    if (index < 0 || configure == 0 || width == 0U || height == 0U ||
        width > REIST_GUI_SURFACE_MAX_WIDTH ||
        height > REIST_GUI_SURFACE_MAX_HEIGHT)
        return DESKTOP_SURFACE_EINVAL;
    desktop_surface_slot_t *slot = &manager->slots[index];
    if (slot->acknowledged_serial != slot->configured_serial)
        return DESKTOP_SURFACE_ESTATE;
    slot->width = width;
    slot->height = height;
    slot->configured_serial = next_nonzero(&manager->next_configure_serial);
    slot->acknowledged_serial = 0U;
    slot->configure_sent = 0U;
    slot->pending_paint_count = 0U;
    slot->paint_active = 0U;
    *configure = (reist_gui_surface_configure_t){
        slot->configured_serial, width, height, 0U, 0U};
    return DESKTOP_SURFACE_OK;
}

int desktop_surface_attach(desktop_surface_manager_t *manager,
                           reist_gui_surface_owner_t owner,
                           reist_gui_surface_handle_t handle,
                           uint32_t buffer_id, uint32_t buffer_generation,
                           uint32_t width, uint32_t height) {
    int index = find_slot(manager, owner, handle);
    if (index < 0 || buffer_id == 0U || buffer_generation == 0U ||
        width != manager->slots[index].width ||
        height != manager->slots[index].height ||
        manager->slots[index].acknowledged_serial == 0U)
        return DESKTOP_SURFACE_ESTATE;
    int buffer_index = find_buffer(manager, owner, buffer_id,
                                   buffer_generation);
    if (buffer_index < 0 ||
        manager->buffers[buffer_index].descriptor.width != width ||
        manager->buffers[buffer_index].descriptor.height != height)
        return DESKTOP_SURFACE_ESTALE;
    if (width > REIST_GUI_SURFACE_MAX_WIDTH ||
        height > REIST_GUI_SURFACE_MAX_HEIGHT || width > UINT32_MAX / 4U ||
        height > UINT32_MAX / (width * 4U) ||
        width * height * 4U > REIST_GUI_SURFACE_MAX_BUFFER_BYTES)
        return DESKTOP_SURFACE_EINVAL;
    desktop_surface_slot_t *slot = &manager->slots[index];
    slot->attached_buffer = buffer_id;
    slot->attached_generation = buffer_generation;
    slot->attached = 1U;
    return DESKTOP_SURFACE_OK;
}

int desktop_surface_input_enqueue(desktop_surface_manager_t *manager,
                                  reist_gui_surface_owner_t owner,
                                  reist_gui_surface_handle_t handle,
                                  const reist_gui_surface_input_t *event) {
    int index = find_slot(manager, owner, handle);
    if (index < 0 || event == 0 || event->serial == 0U ||
        event->type < REIST_GUI_SURFACE_INPUT_POINTER_MOTION ||
        event->type > REIST_GUI_SURFACE_INPUT_KEYBOARD)
        return DESKTOP_SURFACE_EINVAL;
    desktop_surface_slot_t *slot = &manager->slots[index];
    if (event->type != REIST_GUI_SURFACE_INPUT_KEYBOARD &&
        (event->x < 0 || event->y < 0 ||
         (uint32_t)event->x >= slot->width ||
         (uint32_t)event->y >= slot->height))
        return DESKTOP_SURFACE_EINVAL;
    if (event->type == REIST_GUI_SURFACE_INPUT_POINTER_BUTTON &&
        event->button == 0U)
        return DESKTOP_SURFACE_EINVAL;
    if (event->type == REIST_GUI_SURFACE_INPUT_POINTER_MOTION &&
        slot->event_count != 0U) {
        uint32_t last = (slot->event_head + slot->event_count - 1U) %
            REIST_GUI_SURFACE_MAX_PENDING_EVENTS;
        if (slot->pending_events[last].type ==
                REIST_GUI_SURFACE_INPUT_POINTER_MOTION) {
            slot->pending_events[last] = *event;
            return DESKTOP_SURFACE_OK;
        }
    }
    if (slot->event_count >= REIST_GUI_SURFACE_MAX_PENDING_EVENTS &&
        slot->pending_events[slot->event_head].type ==
            REIST_GUI_SURFACE_INPUT_POINTER_MOTION) {
        slot->event_head = (slot->event_head + 1U) %
            REIST_GUI_SURFACE_MAX_PENDING_EVENTS;
        --slot->event_count;
    }
    if (slot->event_count >= REIST_GUI_SURFACE_MAX_PENDING_EVENTS)
        return DESKTOP_SURFACE_ECAPACITY;
    uint32_t tail = (slot->event_head + slot->event_count) %
        REIST_GUI_SURFACE_MAX_PENDING_EVENTS;
    slot->pending_events[tail] = *event;
    ++slot->event_count;
    return DESKTOP_SURFACE_OK;
}

int desktop_surface_input_dequeue(desktop_surface_manager_t *manager,
                                  reist_gui_surface_owner_t owner,
                                  reist_gui_surface_handle_t handle,
                                  reist_gui_surface_input_t *event) {
    int index = find_slot(manager, owner, handle);
    if (index < 0 || event == 0) return DESKTOP_SURFACE_EINVAL;
    desktop_surface_slot_t *slot = &manager->slots[index];
    if (slot->event_count == 0U) return DESKTOP_SURFACE_ESTATE;
    *event = slot->pending_events[slot->event_head];
    slot->event_head = (slot->event_head + 1U) %
        REIST_GUI_SURFACE_MAX_PENDING_EVENTS;
    --slot->event_count;
    return DESKTOP_SURFACE_OK;
}

int desktop_surface_set_title(desktop_surface_manager_t *manager,
                              reist_gui_surface_owner_t owner,
                              reist_gui_surface_handle_t handle,
                              const char *title, uint32_t length) {
    int index = find_slot(manager, owner, handle);
    if (index < 0 || title == 0 || length == 0U ||
        length >= REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY)
        return DESKTOP_SURFACE_EINVAL;
    copy_bounded_text(manager->slots[index].title, title, length);
    return DESKTOP_SURFACE_OK;
}

int desktop_surface_paint_begin(desktop_surface_manager_t *manager,
                                reist_gui_surface_owner_t owner,
                                reist_gui_surface_handle_t handle) {
    int index = find_slot(manager, owner, handle);
    if (index < 0 || manager->slots[index].acknowledged_serial == 0U)
        return DESKTOP_SURFACE_ESTATE;
    desktop_surface_slot_t *slot = &manager->slots[index];
    slot->pending_paint_count = 0U;
    slot->paint_active = 1U;
    return DESKTOP_SURFACE_OK;
}

static desktop_surface_paint_command_t *reserve_paint_command(
    desktop_surface_slot_t *slot) {
    if (slot == 0 || !slot->paint_active ||
        slot->pending_paint_count >= REIST_GUI_SURFACE_MAX_PAINT_COMMANDS)
        return 0;
    return &slot->pending_paint[slot->pending_paint_count++];
}

int desktop_surface_paint_fill(desktop_surface_manager_t *manager,
                               reist_gui_surface_owner_t owner,
                               reist_gui_surface_handle_t handle,
                               reist_gui_rect_t rect, uint32_t color) {
    int index = find_slot(manager, owner, handle);
    if (index < 0 || !valid_local_rect(&manager->slots[index], rect))
        return DESKTOP_SURFACE_EINVAL;
    desktop_surface_paint_command_t *command =
        reserve_paint_command(&manager->slots[index]);
    if (command == 0) return DESKTOP_SURFACE_ECAPACITY;
    command->type = DESKTOP_SURFACE_PAINT_FILL;
    command->rect = rect;
    command->foreground = color;
    command->background = 0U;
    command->text_length = 0U;
    copy_bounded_text(command->text, "", 0U);
    return DESKTOP_SURFACE_OK;
}

int desktop_surface_paint_text(desktop_surface_manager_t *manager,
                               reist_gui_surface_owner_t owner,
                               reist_gui_surface_handle_t handle,
                               reist_gui_rect_t rect, uint32_t foreground,
                               uint32_t background, const char *text,
                               uint32_t length) {
    int index = find_slot(manager, owner, handle);
    if (index < 0 || text == 0 || length == 0U ||
        length >= REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY ||
        !valid_local_rect(&manager->slots[index], rect))
        return DESKTOP_SURFACE_EINVAL;
    desktop_surface_paint_command_t *command =
        reserve_paint_command(&manager->slots[index]);
    if (command == 0) return DESKTOP_SURFACE_ECAPACITY;
    command->type = DESKTOP_SURFACE_PAINT_TEXT;
    command->rect = rect;
    command->foreground = foreground;
    command->background = background;
    command->text_length = length;
    copy_bounded_text(command->text, text, length);
    return DESKTOP_SURFACE_OK;
}

int desktop_surface_paint_commit(desktop_surface_manager_t *manager,
                                 reist_gui_surface_owner_t owner,
                                 reist_gui_surface_handle_t handle) {
    int index = find_slot(manager, owner, handle);
    if (index < 0 || !manager->slots[index].paint_active)
        return DESKTOP_SURFACE_ESTATE;
    desktop_surface_slot_t *slot = &manager->slots[index];
    for (uint32_t command = 0U; command < slot->pending_paint_count;
         ++command)
        slot->committed_paint[command] = slot->pending_paint[command];
    slot->committed_paint_count = slot->pending_paint_count;
    slot->paint_active = 0U;
    slot->paint_generation = next_nonzero(&slot->paint_generation);
    return DESKTOP_SURFACE_OK;
}

int desktop_surface_damage(desktop_surface_manager_t *manager,
                            reist_gui_surface_owner_t owner,
                            reist_gui_surface_handle_t handle,
                            reist_gui_rect_t damage) {
    int index = find_slot(manager, owner, handle);
    if (index < 0 || damage.width == 0U || damage.height == 0U ||
        damage.x < 0 || damage.y < 0 ||
        (uint32_t)damage.x >= manager->slots[index].width ||
        (uint32_t)damage.y >= manager->slots[index].height ||
        damage.width > manager->slots[index].width - (uint32_t)damage.x ||
        damage.height > manager->slots[index].height - (uint32_t)damage.y)
        return DESKTOP_SURFACE_EINVAL;
    desktop_surface_slot_t *slot = &manager->slots[index];
    if (slot->damage.count >= REIST_GUI_SURFACE_MAX_DAMAGE)
        return DESKTOP_SURFACE_ECAPACITY;
    slot->damage.rects[slot->damage.count++] = damage;
    return DESKTOP_SURFACE_OK;
}

int desktop_surface_commit(desktop_surface_manager_t *manager,
                            reist_gui_surface_owner_t owner,
                            reist_gui_surface_handle_t handle,
                            desktop_surface_commit_result_t *result) {
    int index = find_slot(manager, owner, handle);
    if (index < 0 || !result) return DESKTOP_SURFACE_EINVAL;
    desktop_surface_slot_t *slot = &manager->slots[index];
    if (!slot->attached || slot->damage.count == 0U)
        return DESKTOP_SURFACE_ESTATE;
    result->committed = 1U;
    result->buffer_id = slot->attached_buffer;
    result->buffer_generation = slot->attached_generation;
    result->released_buffer_id = slot->committed_buffer;
    result->released_buffer_generation =
        slot->committed_buffer_generation;
    result->damage.count = slot->damage.count;
    result->damage.reserved = slot->damage.reserved;
    for (uint32_t i = 0U; i < slot->damage.count; ++i)
        result->damage.rects[i] = slot->damage.rects[i];
    slot->committed = 1U;
    slot->committed_buffer = slot->attached_buffer;
    slot->committed_buffer_generation = slot->attached_generation;
    slot->paint_generation = next_nonzero(&slot->paint_generation);
    slot->damage.count = 0U;
    slot->attached_buffer = 0U;
    slot->attached_generation = 0U;
    slot->attached = 0U;
    return DESKTOP_SURFACE_OK;
}

int desktop_surface_destroy(desktop_surface_manager_t *manager,
                            reist_gui_surface_owner_t owner,
                            reist_gui_surface_handle_t handle) {
    int index = find_slot(manager, owner, handle);
    if (index < 0) return DESKTOP_SURFACE_ESTALE;
    uint8_t *bytes = (uint8_t *)&manager->slots[index];
    for (uint32_t i = 0U; i < sizeof(manager->slots[index]); ++i)
        bytes[i] = 0U;
    return DESKTOP_SURFACE_OK;
}

int desktop_surface_dispatch_message(
    desktop_surface_manager_t *manager,
    reist_gui_surface_owner_t owner,
    const reist_gui_surface_message_t *request,
    reist_gui_surface_message_t *response) {
    if (!manager || !request || !response || !valid_owner(owner) ||
        request->protocol_version != REIST_GUI_SURFACE_PROTOCOL_VERSION ||
        request->message_size != sizeof(*request)) return DESKTOP_SURFACE_EINVAL;
    uint8_t *response_bytes = (uint8_t *)response;
    for (uint32_t i = 0U; i < sizeof(*response); ++i) response_bytes[i] = 0U;
    response->protocol_version = REIST_GUI_SURFACE_PROTOCOL_VERSION;
    response->message_size = sizeof(*response);
    response->surface = request->surface;
    int result = DESKTOP_SURFACE_EINVAL;
    if (request->type == REIST_GUI_SURFACE_CREATE) {
        reist_gui_surface_configure_t configure;
        result = desktop_surface_create(
            manager, owner, request->flags, request->width, request->height,
            &response->surface, &configure);
        if (result == DESKTOP_SURFACE_OK) {
            response->type = REIST_GUI_SURFACE_CONFIGURE;
            response->serial = configure.serial;
            response->width = configure.width;
            response->height = configure.height;
        }
    } else if (request->type == REIST_GUI_SURFACE_ACK_CONFIGURE) {
        result = desktop_surface_ack_configure(
            manager, owner, request->surface, request->serial);
        response->type = REIST_GUI_SURFACE_ACK_CONFIGURE;
        response->serial = request->serial;
    } else if (request->type == REIST_GUI_SURFACE_BUFFER_CREATE) {
        reist_gui_surface_buffer_t buffer = {
            REIST_GUI_SURFACE_BUFFER_API_VERSION, sizeof(buffer),
            request->buffer_id, request->buffer_generation,
            request->width, request->height, request->stride_bytes,
            request->format, request->byte_size, 0U};
        result = desktop_surface_buffer_create(manager, owner, &buffer);
        response->type = REIST_GUI_SURFACE_BUFFER_CREATE;
        response->buffer_id = request->buffer_id;
        response->buffer_generation = request->buffer_generation;
    } else if (request->type == REIST_GUI_SURFACE_BUFFER_DESTROY) {
        result = desktop_surface_buffer_destroy(
            manager, owner, request->buffer_id,
            request->buffer_generation);
        response->type = REIST_GUI_SURFACE_BUFFER_DESTROY;
    } else if (request->type == REIST_GUI_SURFACE_SET_TITLE) {
        result = desktop_surface_set_title(
            manager, owner, request->surface,
            (const char *)&request->input, request->byte_size);
        response->type = REIST_GUI_SURFACE_SET_TITLE;
    } else if (request->type == REIST_GUI_SURFACE_PAINT_BEGIN) {
        result = desktop_surface_paint_begin(
            manager, owner, request->surface);
        response->type = REIST_GUI_SURFACE_PAINT_BEGIN;
    } else if (request->type == REIST_GUI_SURFACE_PAINT_FILL) {
        result = desktop_surface_paint_fill(
            manager, owner, request->surface, request->damage,
            request->flags);
        response->type = REIST_GUI_SURFACE_PAINT_FILL;
    } else if (request->type == REIST_GUI_SURFACE_PAINT_TEXT) {
        result = desktop_surface_paint_text(
            manager, owner, request->surface, request->damage,
            request->flags, request->buffer_id,
            (const char *)&request->input, request->byte_size);
        response->type = REIST_GUI_SURFACE_PAINT_TEXT;
    } else if (request->type == REIST_GUI_SURFACE_PAINT_COMMIT) {
        result = desktop_surface_paint_commit(
            manager, owner, request->surface);
        response->type = REIST_GUI_SURFACE_PAINT_COMMIT;
    } else if (request->type == REIST_GUI_SURFACE_ATTACH) {
        result = desktop_surface_attach(
            manager, owner, request->surface, request->buffer_id,
            request->buffer_generation, request->width, request->height);
        response->type = REIST_GUI_SURFACE_ATTACH;
    } else if (request->type == REIST_GUI_SURFACE_DAMAGE) {
        result = desktop_surface_damage(
            manager, owner, request->surface, request->damage);
        response->type = REIST_GUI_SURFACE_DAMAGE;
    } else if (request->type == REIST_GUI_SURFACE_COMMIT) {
        desktop_surface_commit_result_t committed;
        result = desktop_surface_commit(
            manager, owner, request->surface, &committed);
        response->type = REIST_GUI_SURFACE_BUFFER_RELEASE;
        if (result == DESKTOP_SURFACE_OK) {
            response->buffer_id = committed.released_buffer_id;
            response->buffer_generation =
                committed.released_buffer_generation;
            response->damage = committed.damage.rects[0];
        }
    } else if (request->type == REIST_GUI_SURFACE_DESTROY) {
        result = desktop_surface_destroy(
            manager, owner, request->surface);
        response->type = REIST_GUI_SURFACE_DESTROY;
    } else {
        response->type = request->type;
    }
    response->flags = (uint32_t)result;
    return result;
}
