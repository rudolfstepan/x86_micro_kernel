/** @file surface_client.c @brief Bounded Surface IPC client wrapper. */
#include "reist/gui/surface_client.h"
#include "reist/gui/font_catalog.h"

static void clear_bytes(void *memory, uint32_t size) {
    uint8_t *bytes = (uint8_t *)memory;
    for (uint32_t i = 0U; i < size; ++i) bytes[i] = 0U;
}

int reist_gui_surface_endpoint_from_argv(int argc, char **argv,
                                         x86os_ipc_handle_t *endpoint) {
    static const char prefix[] = "--reist-surface=";
    if (endpoint == 0 || argc < 1 || argv == 0) return -22;
    *endpoint = 0U;
    for (int argument = 0; argument < argc; ++argument) {
        const char *text = argv[argument];
        if (text == 0) continue;
        uint32_t index = 0U;
        while (prefix[index] != '\0' && text[index] == prefix[index]) ++index;
        if (prefix[index] != '\0') continue;
        if (text[index] == '\0') return -22;
        uint32_t value = 0U;
        uint32_t digits = 0U;
        while (text[index] >= '0' && text[index] <= '9') {
            uint32_t digit = (uint32_t)(text[index] - '0');
            if (value > (UINT32_MAX - digit) / 10U) return -34;
            value = value * 10U + digit;
            ++index;
            ++digits;
        }
        if (digits == 0U || text[index] != '\0' || value == 0U) return -22;
        *endpoint = value;
        return 0;
    }
    return -2;
}

int reist_gui_surface_buffer_validate(
    const reist_gui_surface_buffer_t *buffer) {
    if (buffer == 0 || buffer->version != REIST_GUI_SURFACE_BUFFER_API_VERSION ||
        buffer->struct_size != sizeof(*buffer) || buffer->capability_id == 0U ||
        buffer->capability_generation == 0U || buffer->width == 0U ||
        buffer->height == 0U || buffer->width > REIST_GUI_SURFACE_MAX_WIDTH ||
        buffer->height > REIST_GUI_SURFACE_MAX_HEIGHT ||
        buffer->format != REIST_GUI_SURFACE_BUFFER_FORMAT_XRGB8888 ||
        buffer->width > UINT32_MAX / 4U ||
        buffer->stride_bytes < buffer->width * 4U ||
        buffer->height > UINT32_MAX / buffer->stride_bytes ||
        buffer->byte_size != buffer->height * buffer->stride_bytes ||
        buffer->byte_size > REIST_GUI_SURFACE_MAX_BUFFER_BYTES)
        return -22;
    return 0;
}

static int valid_client(const reist_gui_surface_client_t *client) {
    return client && client->connected && client->endpoint != 0U;
}

static void prepare(reist_gui_surface_message_t *message, uint32_t type,
                    const reist_gui_surface_client_t *client) {
    clear_bytes(message, sizeof(*message));
    message->protocol_version = REIST_GUI_SURFACE_PROTOCOL_VERSION;
    message->message_size = sizeof(*message);
    message->type = type;
    if (client) message->surface = client->surface;
}

static int receive_wire_message(reist_gui_surface_client_t *client,
                                reist_gui_surface_message_t *message,
                                uint32_t timeout_ms) {
    if (!valid_client(client) || !message) return -22;
    x86os_ipc_message_t ipc;
    clear_bytes(&ipc, sizeof(ipc));
    ipc.version = X86OS_IPC_MESSAGE_VERSION;
    ipc.struct_size = sizeof(ipc);
    int result = x86os_ipc_receive_timeout(client->endpoint, &ipc, timeout_ms);
    if (result != 0 || ipc.version != X86OS_IPC_MESSAGE_VERSION ||
        ipc.struct_size != sizeof(ipc) || ipc.length != sizeof(*message))
        return result != 0 ? result : -84;
    uint8_t *destination = (uint8_t *)message;
    for (uint32_t i = 0U; i < sizeof(*message); ++i)
        destination[i] = ipc.payload[i];
    if (message->protocol_version != REIST_GUI_SURFACE_PROTOCOL_VERSION ||
        message->message_size != sizeof(*message)) return -84;
    if (message->type==REIST_GUI_SURFACE_INPUT &&
        message->input.type==REIST_GUI_SURFACE_INPUT_POINTER_SCROLL &&
        !reist_gui_surface_scroll_valid(&message->input)) return -84;
    return 0;
}

static int defer_message(reist_gui_surface_client_t *client,
                         const reist_gui_surface_message_t *message) {
    if (!valid_client(client) || message == 0) return -22;
    reist_gui_surface_client_t *queue = client->event_owner;
    if (queue == 0 || queue->deferred_count >=
            REIST_GUI_SURFACE_MAX_PENDING_EVENTS)
        return -75;
    uint32_t tail = (queue->deferred_head + queue->deferred_count) %
        REIST_GUI_SURFACE_MAX_PENDING_EVENTS;
    queue->deferred[tail] = *message;
    ++queue->deferred_count;
    return 0;
}

static int receive_message(reist_gui_surface_client_t *client,
                           reist_gui_surface_message_t *message,
                           uint32_t timeout_ms) {
    if (!valid_client(client) || message == 0) return -22;
    reist_gui_surface_client_t *queue = client->event_owner;
    if (queue == 0) return -22;
    if (queue->deferred_count != 0U) {
        *message = queue->deferred[queue->deferred_head];
        queue->deferred_head = (queue->deferred_head + 1U) %
            REIST_GUI_SURFACE_MAX_PENDING_EVENTS;
        --queue->deferred_count;
        return 0;
    }
    return receive_wire_message(client, message, timeout_ms);
}

static uint32_t deferred_response_type(uint32_t type) {
    return type == REIST_GUI_SURFACE_ENABLE_SCROLL || type == REIST_GUI_SURFACE_INPUT ||
        type == REIST_GUI_SURFACE_CLOSE ||
        type == REIST_GUI_SURFACE_CONFIGURE ||
        type == REIST_GUI_SURFACE_ACK_CONFIGURE ||
        type == REIST_GUI_SURFACE_BUFFER_CREATE ||
        type == REIST_GUI_SURFACE_BUFFER_DESTROY ||
        type == REIST_GUI_SURFACE_SET_TITLE ||
        type == REIST_GUI_SURFACE_PAINT_BEGIN ||
        type == REIST_GUI_SURFACE_PAINT_FILL ||
        type == REIST_GUI_SURFACE_PAINT_TEXT ||
        type == REIST_GUI_SURFACE_PAINT_FONT_TEXT ||
        type == REIST_GUI_SURFACE_PAINT_COMMIT ||
        type == REIST_GUI_SURFACE_PAINT_OVERLAY_BEGIN ||
        type == REIST_GUI_SURFACE_PAINT_OVERLAY_COMMIT ||
        type == REIST_GUI_SURFACE_PAINT_DYNAMIC_BEGIN ||
        type == REIST_GUI_SURFACE_PAINT_DYNAMIC_COMMIT ||
        type == REIST_GUI_SURFACE_PAINT_HOVER_BEGIN ||
        type == REIST_GUI_SURFACE_PAINT_HOVER_COMMIT ||
        type == REIST_GUI_SURFACE_ATTACH ||
        type == REIST_GUI_SURFACE_DAMAGE ||
        type == REIST_GUI_SURFACE_BUFFER_RELEASE ||
        type == REIST_GUI_SURFACE_DESTROY;
}

static int asynchronous_paint_error(
    const reist_gui_surface_client_t *client,
    const reist_gui_surface_message_t *response) {
    if (client == 0 || response == 0 ||
        (response->type != REIST_GUI_SURFACE_PAINT_FILL &&
         response->type != REIST_GUI_SURFACE_PAINT_TEXT &&
         response->type != REIST_GUI_SURFACE_PAINT_FONT_TEXT) ||
        response->surface.id != client->surface.id ||
        response->surface.generation != client->surface.generation)
        return 0;
    return (int32_t)response->flags;
}

static int send_message(reist_gui_surface_client_t *client,
                        const reist_gui_surface_message_t *message) {
    if (!valid_client(client) || !message) return -22;
    x86os_ipc_message_t ipc;
    clear_bytes(&ipc, sizeof(ipc));
    ipc.version = X86OS_IPC_MESSAGE_VERSION;
    ipc.struct_size = sizeof(ipc);
    ipc.length = sizeof(*message);
    const uint8_t *source = (const uint8_t *)message;
    for (uint32_t i = 0U; i < sizeof(*message); ++i) ipc.payload[i] = source[i];
    int result = x86os_ipc_send_timeout(client->endpoint, &ipc, 0U);
    if (result != -11) return result;

    /* The endpoint has one shared bidirectional queue. When desktop input
     * fills it, only this client can free space: a blocking send would wait
     * on the peer even though the peer cannot receive its own messages.
     * Preserve input (including other Surfaces on this endpoint) in the
     * existing ordered event-owner queue; never dispatch reentrantly here.
     * The fast path needs no clock syscall. Backpressure retains the former
     * 500-ms send budget, plus a work cap if the clock stops progressing. */
    uint64_t started_ms = 0U;
    result = x86os_monotonic_ms(&started_ms);
    if (result != 0) return result;
    for (uint32_t attempt = 0U;
         attempt < 500U + REIST_GUI_SURFACE_MAX_PENDING_EVENTS; ++attempt) {
        uint64_t now_ms = 0U;
        result = x86os_monotonic_ms(&now_ms);
        if (result != 0) return result;
        if (now_ms < started_ms) return -5;
        if (now_ms - started_ms >= 500U) return -110;
        reist_gui_surface_client_t *queue = client->event_owner;
        if (queue == 0 || queue->deferred_count >=
                REIST_GUI_SURFACE_MAX_PENDING_EVENTS) return -75;
        reist_gui_surface_message_t incoming;
        result = receive_wire_message(client, &incoming, 0U);
        if (result == 0) {
            int error = asynchronous_paint_error(client, &incoming);
            if (error != 0) return error;
            if (!deferred_response_type(incoming.type)) return -84;
            result = defer_message(client, &incoming);
            if (result != 0) return result;
        } else if (result == -11) {
            /* Queue contains our requests, so let the broker consume them. */
            result = x86os_sleep_ms(1U);
            if (result != 0) return result;
        } else return result;
        result = x86os_monotonic_ms(&now_ms);
        if (result != 0) return result;
        if (now_ms < started_ms) return -5;
        if (now_ms - started_ms >= 500U) return -110;
        result = x86os_ipc_send_timeout(client->endpoint, &ipc, 0U);
        if (result != -11) return result;
    }
    return -110;
}

static int transact_response(reist_gui_surface_client_t *client,
                             const reist_gui_surface_message_t *request,
                             uint32_t expected_type,
                             reist_gui_surface_message_t *response_out) {
    int result = send_message(client, request);
    if (result != 0) return result;
    for (uint32_t attempt = 0U;
         attempt <= REIST_GUI_SURFACE_MAX_PENDING_EVENTS; ++attempt) {
        reist_gui_surface_message_t response;
        result = receive_wire_message(client, &response, 500U);
        if (result != 0) return result;
        if (response.type == expected_type &&
            response.surface.id == client->surface.id &&
            response.surface.generation == client->surface.generation) {
            if (response_out != 0) *response_out = response;
            return (int32_t)response.flags;
        }
        int asynchronous_error = asynchronous_paint_error(client, &response);
        if (asynchronous_error != 0) return asynchronous_error;
        if (!deferred_response_type(response.type)) return -84;
        result = defer_message(client, &response);
        if (result != 0) return result;
    }
    return -75;
}

static int transact(reist_gui_surface_client_t *client,
                    const reist_gui_surface_message_t *request,
                    uint32_t expected_type) {
    return transact_response(client, request, expected_type, 0);
}

static uint32_t bounded_text_length(const char *text, uint32_t capacity) {
    uint32_t length = 0U;
    if (text == 0) return 0U;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

int reist_gui_surface_client_init(reist_gui_surface_client_t *client,
                                  x86os_ipc_handle_t endpoint) {
    if (!client || endpoint == 0U) return -22;
    clear_bytes(client, sizeof(*client));
    client->endpoint = endpoint;
    client->connected = 1U;
    client->event_owner = client;
    return 0;
}

int reist_gui_surface_client_init_shared(
    reist_gui_surface_client_t *client,
    reist_gui_surface_client_t *event_owner) {
    if (client == 0 || !valid_client(event_owner) ||
        event_owner->event_owner == 0) return -22;
    clear_bytes(client, sizeof(*client));
    client->endpoint = event_owner->endpoint;
    client->connected = 1U;
    client->event_owner = event_owner->event_owner;
    return 0;
}

static int create_surface(reist_gui_surface_client_t *client, uint32_t role,
                          reist_gui_surface_handle_t parent,
                          uint32_t width, uint32_t height) {
    if (!valid_client(client) ||
        (role != REIST_GUI_SURFACE_ROLE_TOPLEVEL &&
         role != REIST_GUI_SURFACE_ROLE_DIALOG) || width == 0U ||
        height == 0U || width > REIST_GUI_SURFACE_MAX_WIDTH ||
        height > REIST_GUI_SURFACE_MAX_HEIGHT ||
        (role == REIST_GUI_SURFACE_ROLE_TOPLEVEL &&
         (parent.id != 0U || parent.generation != 0U)) ||
        (role == REIST_GUI_SURFACE_ROLE_DIALOG &&
         (parent.id == 0U || parent.generation == 0U))) return -22;
    reist_gui_surface_message_t request;
    prepare(&request, REIST_GUI_SURFACE_CREATE, client);
    request.flags = role;
    request.parent_surface = parent;
    request.width = width;
    request.height = height;
    int result = send_message(client, &request);
    if (result != 0) return result;
    for (uint32_t attempt = 0U;
         attempt <= REIST_GUI_SURFACE_MAX_PENDING_EVENTS; ++attempt) {
        reist_gui_surface_message_t response;
        result = receive_wire_message(client, &response, 500U);
        if (result != 0) return result;
        if (response.type == REIST_GUI_SURFACE_CONFIGURE &&
            (int32_t)response.flags == 0 && response.surface.id != 0U &&
            response.surface.generation != 0U && response.serial != 0U &&
            response.width != 0U && response.height != 0U &&
            (role != REIST_GUI_SURFACE_ROLE_DIALOG ||
             (response.surface.id != parent.id ||
              response.surface.generation != parent.generation))) {
            client->surface = response.surface;
            client->configured_serial = response.serial;
            client->width = response.width;
            client->height = response.height;
            return 0;
        }
        if (!deferred_response_type(response.type)) return -84;
        result = defer_message(client, &response);
        if (result != 0) return result;
    }
    return -75;
}

int reist_gui_surface_client_create(reist_gui_surface_client_t *client,
                                    uint32_t role, uint32_t width,
                                    uint32_t height) {
    if (role != REIST_GUI_SURFACE_ROLE_TOPLEVEL) return -22;
    return create_surface(client, role,
        (reist_gui_surface_handle_t){0U, 0U}, width, height);
}

int reist_gui_surface_client_create_dialog(
    reist_gui_surface_client_t *client,
    reist_gui_surface_handle_t parent, uint32_t width, uint32_t height) {
    return create_surface(client, REIST_GUI_SURFACE_ROLE_DIALOG,
                          parent, width, height);
}

int reist_gui_surface_client_ack_configure(reist_gui_surface_client_t *client,
                                           uint32_t serial) {
    if (!valid_client(client) || serial == 0U ||
        serial != client->configured_serial) return -22;
    reist_gui_surface_message_t message;
    prepare(&message, REIST_GUI_SURFACE_ACK_CONFIGURE, client);
    message.serial = serial;
    int result = send_message(client, &message);
    if (result != 0) return result;

    /* ACK_CONFIGURE is a state-changing compositor transaction.  Do not
     * publish the serial locally merely because the request entered the IPC
     * queue: wait until the compositor has validated and applied it.  This
     * also consumes the protocol reply so it cannot be mistaken for a later
     * input event on the bidirectional endpoint. */
    for (uint32_t attempt = 0U;
         attempt <= REIST_GUI_SURFACE_MAX_PENDING_EVENTS; ++attempt) {
        reist_gui_surface_message_t response;
        result = receive_wire_message(client, &response, 500U);
        if (result != 0) return result;
        if (response.type == REIST_GUI_SURFACE_ACK_CONFIGURE &&
            response.surface.id == client->surface.id &&
            response.surface.generation == client->surface.generation &&
            response.serial == serial) {
            result = (int32_t)response.flags;
            if (result == 0) client->acknowledged_serial = serial;
            return result;
        }
        int asynchronous_error = asynchronous_paint_error(client, &response);
        if (asynchronous_error != 0) return asynchronous_error;
        if (!deferred_response_type(response.type)) return -84;
        result = defer_message(client, &response);
        if (result != 0) return result;
    }
    return -75;
}

int reist_gui_surface_client_accept_configure(
    reist_gui_surface_client_t *client,
    const reist_gui_surface_message_t *configure) {
    if (!valid_client(client) || configure == 0 ||
        configure->type != REIST_GUI_SURFACE_CONFIGURE ||
        configure->surface.id != client->surface.id ||
        configure->surface.generation != client->surface.generation ||
        configure->serial == 0U || configure->width == 0U ||
        configure->height == 0U ||
        configure->width > REIST_GUI_SURFACE_MAX_WIDTH ||
        configure->height > REIST_GUI_SURFACE_MAX_HEIGHT)
        return -22;
    uint32_t old_serial = client->configured_serial;
    uint32_t old_width = client->width;
    uint32_t old_height = client->height;
    client->configured_serial = configure->serial;
    client->width = configure->width;
    client->height = configure->height;
    int result = reist_gui_surface_client_ack_configure(
        client, configure->serial);
    if (result != 0) {
        client->configured_serial = old_serial;
        client->width = old_width;
        client->height = old_height;
    }
    return result;
}

int reist_gui_surface_client_set_title(reist_gui_surface_client_t *client,
                                       const char *title) {
    uint32_t length = bounded_text_length(
        title, REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY);
    if (!valid_client(client) || title == 0 || length == 0U ||
        length >= REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY)
        return -22;
    reist_gui_surface_message_t request;
    prepare(&request, REIST_GUI_SURFACE_SET_TITLE, client);
    request.byte_size = length;
    uint8_t *destination = (uint8_t *)&request.input;
    for (uint32_t i = 0U; i < length; ++i)
        destination[i] = (uint8_t)title[i];
    return transact(client, &request, REIST_GUI_SURFACE_SET_TITLE);
}

int reist_gui_surface_client_enable_scroll(reist_gui_surface_client_t *client) {
    if (!valid_client(client) || !client->acknowledged_serial) return -22;
    reist_gui_surface_message_t request,response;
    prepare(&request,REIST_GUI_SURFACE_ENABLE_SCROLL,client);
    request.serial=REIST_GUI_SURFACE_SCROLL_VERSION;
    int result=transact_response(client,&request,request.type,&response);
    if (result) return result;
    return response.serial==REIST_GUI_SURFACE_SCROLL_VERSION ? 0 : -84;
}

int reist_gui_surface_client_paint_begin(reist_gui_surface_client_t *client) {
    return reist_gui_surface_client_paint_begin_layer(
        client, REIST_GUI_SURFACE_PAINT_LAYER_BASE);
}

int reist_gui_surface_client_paint_begin_layer(
    reist_gui_surface_client_t *client, uint32_t layer) {
    if (!valid_client(client) || client->acknowledged_serial == 0U)
        return -22;
    uint32_t type = REIST_GUI_SURFACE_PAINT_BEGIN;
    if (layer == REIST_GUI_SURFACE_PAINT_LAYER_OVERLAY)
        type = REIST_GUI_SURFACE_PAINT_OVERLAY_BEGIN;
    else if (layer == REIST_GUI_SURFACE_PAINT_LAYER_DYNAMIC)
        type = REIST_GUI_SURFACE_PAINT_DYNAMIC_BEGIN;
    else if (layer == REIST_GUI_SURFACE_PAINT_LAYER_HOVER)
        type = REIST_GUI_SURFACE_PAINT_HOVER_BEGIN;
    else if (layer != REIST_GUI_SURFACE_PAINT_LAYER_BASE) return -22;
    reist_gui_surface_message_t request;
    prepare(&request, type, client);
    return transact(client, &request, type);
}

int reist_gui_surface_client_paint_fill(reist_gui_surface_client_t *client,
                                        reist_gui_rect_t rect,
                                        uint32_t color) {
    if (!valid_client(client) || rect.x < 0 || rect.y < 0 ||
        rect.width == 0U || rect.height == 0U ||
        (uint32_t)rect.x >= client->width ||
        (uint32_t)rect.y >= client->height ||
        rect.width > client->width - (uint32_t)rect.x ||
        rect.height > client->height - (uint32_t)rect.y)
        return -22;
    reist_gui_surface_message_t request;
    prepare(&request, REIST_GUI_SURFACE_PAINT_FILL, client);
    request.damage = rect;
    request.flags = color;
    return send_message(client, &request);
}

int reist_gui_surface_client_paint_text(reist_gui_surface_client_t *client,
                                        int32_t x, int32_t y,
                                        uint32_t maximum_width,
                                        const char *text_value,
                                        uint32_t length,
                                        uint32_t foreground,
                                        uint32_t background) {
    if (!valid_client(client) || text_value == 0 || length == 0U ||
        length >= REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY || x < 0 || y < 0 ||
        maximum_width == 0U || (uint32_t)x >= client->width ||
        (uint32_t)y >= client->height ||
        maximum_width > client->width - (uint32_t)x)
        return -22;
    reist_gui_surface_message_t request;
    prepare(&request, REIST_GUI_SURFACE_PAINT_TEXT, client);
    request.damage = (reist_gui_rect_t){x, y, maximum_width, 1U};
    request.flags = foreground;
    request.buffer_id = background;
    request.byte_size = length;
    uint8_t *destination = (uint8_t *)&request.input;
    for (uint32_t i = 0U; i < length; ++i)
        destination[i] = (uint8_t)text_value[i];
    return send_message(client, &request);
}

int reist_gui_surface_client_paint_font_text(
    reist_gui_surface_client_t *client, int32_t x, int32_t y,
    uint32_t maximum_width, const char *text_value, uint32_t length,
    uint32_t foreground, uint32_t background,
    uint32_t font_family, uint32_t pixel_height) {
    if (!valid_client(client) || text_value == 0 || length == 0U ||
        length >= REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY || x < 0 || y < 0 ||
        maximum_width == 0U || (uint32_t)x >= client->width ||
        (uint32_t)y >= client->height ||
        maximum_width > client->width - (uint32_t)x ||
        pixel_height > client->height - (uint32_t)y ||
        !reist_gui_font_catalog_selection_valid(font_family, pixel_height))
        return -22;
    reist_gui_surface_message_t request;
    prepare(&request, REIST_GUI_SURFACE_PAINT_FONT_TEXT, client);
    request.damage = (reist_gui_rect_t){x, y, maximum_width, pixel_height};
    request.flags = foreground;
    request.buffer_id = background;
    request.format = font_family;
    request.stride_bytes = pixel_height;
    request.byte_size = length;
    uint8_t *destination = (uint8_t *)&request.input;
    for (uint32_t i = 0U; i < length; ++i)
        destination[i] = (uint8_t)text_value[i];
    return send_message(client, &request);
}

int reist_gui_surface_client_paint_commit(reist_gui_surface_client_t *client) {
    return reist_gui_surface_client_paint_commit_layer(
        client, REIST_GUI_SURFACE_PAINT_LAYER_BASE);
}

int reist_gui_surface_client_paint_commit_layer(
    reist_gui_surface_client_t *client, uint32_t layer) {
    if (!valid_client(client) || client->acknowledged_serial == 0U)
        return -22;
    uint32_t type = REIST_GUI_SURFACE_PAINT_COMMIT;
    if (layer == REIST_GUI_SURFACE_PAINT_LAYER_OVERLAY)
        type = REIST_GUI_SURFACE_PAINT_OVERLAY_COMMIT;
    else if (layer == REIST_GUI_SURFACE_PAINT_LAYER_DYNAMIC)
        type = REIST_GUI_SURFACE_PAINT_DYNAMIC_COMMIT;
    else if (layer == REIST_GUI_SURFACE_PAINT_LAYER_HOVER)
        type = REIST_GUI_SURFACE_PAINT_HOVER_COMMIT;
    else if (layer != REIST_GUI_SURFACE_PAINT_LAYER_BASE) return -22;
    reist_gui_surface_message_t request;
    prepare(&request, type, client);
    return transact(client, &request, type);
}

int reist_gui_surface_client_buffer_create(
    reist_gui_surface_client_t *client,
    const reist_gui_surface_buffer_t *buffer) {
    if (!valid_client(client) ||
        reist_gui_surface_buffer_validate(buffer) != 0) return -22;
    reist_gui_surface_message_t message;
    prepare(&message, REIST_GUI_SURFACE_BUFFER_CREATE, client);
    message.buffer_id = buffer->capability_id;
    message.buffer_generation = buffer->capability_generation;
    message.width = buffer->width;
    message.height = buffer->height;
    message.stride_bytes = buffer->stride_bytes;
    message.format = buffer->format;
    message.byte_size = buffer->byte_size;
    return transact(client, &message, REIST_GUI_SURFACE_BUFFER_CREATE);
}

int reist_gui_surface_client_buffer_destroy(
    reist_gui_surface_client_t *client, uint32_t capability_id,
    uint32_t capability_generation) {
    if (!valid_client(client) || capability_id == 0U ||
        capability_generation == 0U) return -22;
    reist_gui_surface_message_t message;
    prepare(&message, REIST_GUI_SURFACE_BUFFER_DESTROY, client);
    message.buffer_id = capability_id;
    message.buffer_generation = capability_generation;
    return transact(client, &message, REIST_GUI_SURFACE_BUFFER_DESTROY);
}

int reist_gui_surface_client_attach(reist_gui_surface_client_t *client,
                                    uint32_t buffer_id,
                                    uint32_t buffer_generation) {
    if (!valid_client(client) || client->acknowledged_serial == 0U ||
        buffer_id == 0U || buffer_generation == 0U) return -22;
    reist_gui_surface_message_t message;
    prepare(&message, REIST_GUI_SURFACE_ATTACH, client);
    message.buffer_id = buffer_id;
    message.buffer_generation = buffer_generation;
    message.width = client->width;
    message.height = client->height;
    return transact(client, &message, REIST_GUI_SURFACE_ATTACH);
}

int reist_gui_surface_client_damage(reist_gui_surface_client_t *client,
                                    reist_gui_rect_t damage) {
    if (!valid_client(client) || damage.width == 0U || damage.height == 0U ||
        damage.x < 0 || damage.y < 0 ||
        (uint32_t)damage.x >= client->width ||
        (uint32_t)damage.y >= client->height ||
        damage.width > client->width - (uint32_t)damage.x ||
        damage.height > client->height - (uint32_t)damage.y) return -22;
    reist_gui_surface_message_t message;
    prepare(&message, REIST_GUI_SURFACE_DAMAGE, client);
    message.damage = damage;
    return transact(client, &message, REIST_GUI_SURFACE_DAMAGE);
}

int reist_gui_surface_client_commit(reist_gui_surface_client_t *client) {
    return reist_gui_surface_client_commit_with_release(client, 0, 0);
}

int reist_gui_surface_client_commit_with_release(
    reist_gui_surface_client_t *client, uint32_t *released_buffer_id,
    uint32_t *released_buffer_generation) {
    if (!valid_client(client) || client->acknowledged_serial == 0U) return -22;
    reist_gui_surface_message_t message;
    prepare(&message, REIST_GUI_SURFACE_COMMIT, client);
    reist_gui_surface_message_t response;
    int result = transact_response(
        client, &message, REIST_GUI_SURFACE_BUFFER_RELEASE, &response);
    if (result == 0) {
        if ((response.buffer_id == 0U) !=
            (response.buffer_generation == 0U)) return -84;
        if (released_buffer_id != 0)
            *released_buffer_id = response.buffer_id;
        if (released_buffer_generation != 0)
            *released_buffer_generation = response.buffer_generation;
    }
    return result;
}

int reist_gui_surface_client_receive(reist_gui_surface_client_t *client,
                                     reist_gui_surface_message_t *message,
                                     uint32_t timeout_ms) {
    return receive_message(client, message, timeout_ms);
}

int reist_gui_surface_client_receive_input(
    reist_gui_surface_client_t *client, reist_gui_surface_input_t *event,
    uint32_t timeout_ms) {
    if (!valid_client(client) || event == 0) return -22;
    reist_gui_surface_message_t message;
    int result = receive_message(client, &message, timeout_ms);
    if (result != 0) return result;
    if (message.type != REIST_GUI_SURFACE_INPUT ||
        message.surface.id != client->surface.id ||
        message.surface.generation != client->surface.generation ||
        message.input.serial == 0U ||
        message.input.type < REIST_GUI_SURFACE_INPUT_POINTER_MOTION ||
        message.input.type > REIST_GUI_SURFACE_INPUT_POINTER_SCROLL)
        return -84;
    *event = message.input;
    return 0;
}

int reist_gui_surface_client_destroy(reist_gui_surface_client_t *client) {
    if (!valid_client(client)) return -22;
    reist_gui_surface_message_t message;
    prepare(&message, REIST_GUI_SURFACE_DESTROY, client);
    int result = send_message(client, &message);
    if (result == 0) client->connected = 0U;
    return result;
}
