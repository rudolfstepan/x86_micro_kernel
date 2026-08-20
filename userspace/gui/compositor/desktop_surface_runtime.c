/** @file desktop_surface_runtime.c @brief Bounded desktop Surface broker. */
#include "desktop_surface_runtime.h"
static void clear_bytes(void *memory, uint32_t size) { uint8_t *b = memory; for (uint32_t i=0;i<size;++i) b[i]=0U; }
static int send_response(x86os_ipc_handle_t endpoint, const reist_gui_surface_message_t *message) {
    x86os_ipc_message_t ipc; clear_bytes(&ipc, sizeof(ipc));
    ipc.version=X86OS_IPC_MESSAGE_VERSION; ipc.struct_size=sizeof(ipc); ipc.length=sizeof(*message);
    const uint8_t *s=(const uint8_t *)message; for (uint32_t i=0;i<sizeof(*message);++i) ipc.payload[i]=s[i];
    return x86os_ipc_send_timeout(endpoint, &ipc, 0U);
}
int desktop_surface_runtime_initialize(desktop_surface_runtime_t *runtime) {
    if (runtime == 0) return DESKTOP_SURFACE_EINVAL;
    clear_bytes(runtime, sizeof(*runtime));
    return 0;
}
int desktop_surface_runtime_reserve(desktop_surface_runtime_t *runtime,
                                    x86os_ipc_handle_t *client_endpoint) {
    if (runtime == 0 || client_endpoint == 0) return DESKTOP_SURFACE_EINVAL;
    for (uint32_t i = 0U; i < DESKTOP_SURFACE_RUNTIME_CAPACITY; ++i) {
        if (runtime->clients[i].active == 0U) {
            x86os_ipc_handle_t endpoint = 0U;
            int status = x86os_ipc_create(&endpoint);
            if (status != 0 || endpoint == 0U)
                return status != 0 ? status : DESKTOP_SURFACE_EINVAL;
            runtime->clients[i].endpoint = endpoint;
            runtime->clients[i].active = 2U;
            *client_endpoint = endpoint;
            return 0;
        }
    }
    return DESKTOP_SURFACE_ECAPACITY;
}

int desktop_surface_runtime_bind(desktop_surface_runtime_t *runtime,
                                 x86os_ipc_handle_t endpoint, int pid) {
    if (runtime == 0 || endpoint == 0U || pid <= 0)
        return DESKTOP_SURFACE_EINVAL;
    uint32_t slot = DESKTOP_SURFACE_RUNTIME_CAPACITY;
    for (uint32_t i = 0U; i < DESKTOP_SURFACE_RUNTIME_CAPACITY; ++i) {
        if (runtime->clients[i].active == 2U &&
            runtime->clients[i].endpoint == endpoint) {
            slot = i;
            break;
        }
    }
    if (slot == DESKTOP_SURFACE_RUNTIME_CAPACITY)
        return DESKTOP_SURFACE_ESTALE;
    x86os_process_identity_t identity;
    clear_bytes(&identity, sizeof(identity));
    int status = x86os_process_identity_of(pid, &identity);
    if (status != 0 || identity.version != 1U ||
        identity.struct_size != sizeof(identity) || identity.pid != pid ||
        identity.generation == 0U)
        return status != 0 ? status : DESKTOP_SURFACE_EINVAL;
    status = x86os_ipc_delegate(
        endpoint, pid, X86OS_IPC_RIGHT_SEND | X86OS_IPC_RIGHT_RECEIVE);
    if (status != 0) return status;
    runtime->clients[slot].owner.pid = pid;
    runtime->clients[slot].owner.process_generation = identity.generation;
    runtime->clients[slot].active = 1U;
    return 0;
}

void desktop_surface_runtime_cancel(desktop_surface_runtime_t *runtime,
                                    x86os_ipc_handle_t endpoint) {
    if (runtime == 0 || endpoint == 0U) return;
    for (uint32_t i = 0U; i < DESKTOP_SURFACE_RUNTIME_CAPACITY; ++i) {
        if (runtime->clients[i].active != 0U &&
            runtime->clients[i].endpoint == endpoint) {
            (void)x86os_ipc_close(endpoint);
            clear_bytes(&runtime->clients[i], sizeof(runtime->clients[i]));
            return;
        }
    }
}
int desktop_surface_runtime_open_for_process(
    desktop_surface_runtime_t *runtime, int pid,
    x86os_ipc_handle_t *client_endpoint) {
    if (runtime == 0 || client_endpoint == 0 || pid <= 0)
        return DESKTOP_SURFACE_EINVAL;
    x86os_ipc_handle_t endpoint = 0U;
    int status = desktop_surface_runtime_reserve(runtime, &endpoint);
    if (status != 0) return status;
    status = desktop_surface_runtime_bind(runtime, endpoint, pid);
    if (status != 0) {
        desktop_surface_runtime_cancel(runtime, endpoint);
        return status;
    }
    *client_endpoint = endpoint;
    return 0;
}
static int poll_client(desktop_surface_runtime_client_t *client, desktop_surface_manager_t *manager) {
    x86os_ipc_message_t ipc;
    clear_bytes(&ipc, sizeof(ipc));
    ipc.version = X86OS_IPC_MESSAGE_VERSION;
    ipc.struct_size = sizeof(ipc);
    int status = x86os_ipc_receive_timeout(client->endpoint, &ipc, 0U);
    if (status == -32) return -32;
    if (status == -11) return 0;
    if (status != 0) return status;
    if (ipc.version!=X86OS_IPC_MESSAGE_VERSION || ipc.struct_size!=sizeof(ipc) || ipc.length!=sizeof(reist_gui_surface_message_t)) return DESKTOP_SURFACE_EINVAL;
    reist_gui_surface_message_t request,response; uint8_t *d=(uint8_t *)&request; for (uint32_t i=0;i<sizeof(request);++i) d[i]=ipc.payload[i]; clear_bytes(&response,sizeof(response));
    status=desktop_surface_dispatch_message(manager,client->owner,&request,&response); response.flags=(uint32_t)status;
    if ((request.type != REIST_GUI_SURFACE_PAINT_FILL &&
         request.type != REIST_GUI_SURFACE_PAINT_TEXT) || status != 0)
        (void)send_response(client->endpoint,&response);
    return status == 0 ? 1 : status;
}
static int send_pending_input(desktop_surface_runtime_client_t *client,
                              desktop_surface_manager_t *manager) {
    for (uint32_t i = 0U; i < DESKTOP_SURFACE_CAPACITY; ++i) {
        desktop_surface_slot_t *slot = &manager->slots[i];
        if (!slot->active || slot->event_count == 0U ||
            slot->owner.pid != client->owner.pid ||
            slot->owner.process_generation !=
                client->owner.process_generation)
            continue;
        reist_gui_surface_message_t message;
        clear_bytes(&message, sizeof(message));
        message.protocol_version = REIST_GUI_SURFACE_PROTOCOL_VERSION;
        message.message_size = sizeof(message);
        message.type = REIST_GUI_SURFACE_INPUT;
        message.surface = slot->handle;
        message.input = slot->pending_events[slot->event_head];
        int status = send_response(client->endpoint, &message);
        if (status != 0) return status;
        slot->event_head = (slot->event_head + 1U) %
            REIST_GUI_SURFACE_MAX_PENDING_EVENTS;
        --slot->event_count;
        return 0;
    }
    return 0;
}
int desktop_surface_runtime_poll(desktop_surface_runtime_t *runtime, desktop_surface_manager_t *manager) {
    if (runtime == 0 || manager == 0) return DESKTOP_SURFACE_EINVAL;
    int result = 0;
    for (uint32_t round = 0U;
         round < DESKTOP_SURFACE_RUNTIME_DRAIN_ROUNDS; ++round) {
        uint32_t processed_round = 0U;
        for (uint32_t i = 0U;
             i < DESKTOP_SURFACE_RUNTIME_CAPACITY; ++i) {
            if (runtime->clients[i].active != 1U) continue;
            int status = 0;
            uint32_t processed_client = 0U;
            for (uint32_t request = 0U;
                 request < X86OS_IPC_QUEUE_DEPTH; ++request) {
                status = poll_client(&runtime->clients[i], manager);
                if (status == 1) {
                    processed_client = 1U;
                    processed_round = 1U;
                    continue;
                }
                break;
            }
            if (status == 1) status = 0;
            if (status == -32) {
                desktop_surface_revoke_owner(manager,
                    runtime->clients[i].owner);
                (void)x86os_ipc_close(runtime->clients[i].endpoint);
                runtime->clients[i].endpoint = 0U;
                runtime->clients[i].active = 0U;
            } else if (status == 0 && !processed_client) {
                status = send_pending_input(
                    &runtime->clients[i], manager);
                if (status != 0 && result == 0) result = status;
            } else if (status != 0 && result == 0) {
                result = status;
            }
        }
        if (!processed_round || result != 0) break;
        /* Every active client received one fair queue-depth-sized slice.
         * Yield once so blocked producers can refill before the next round. */
        if (round + 1U < DESKTOP_SURFACE_RUNTIME_DRAIN_ROUNDS)
            (void)x86os_yield();
    }
    return result;
}
int desktop_surface_runtime_send_close(
    desktop_surface_runtime_t *runtime, reist_gui_surface_owner_t owner,
    reist_gui_surface_handle_t surface) {
    if (runtime == 0 || owner.pid <= 0 || owner.process_generation == 0U ||
        surface.id == 0U || surface.generation == 0U)
        return DESKTOP_SURFACE_EINVAL;
    for (uint32_t i = 0U; i < DESKTOP_SURFACE_RUNTIME_CAPACITY; ++i) {
        desktop_surface_runtime_client_t *client = &runtime->clients[i];
        if (client->active != 1U || client->owner.pid != owner.pid ||
            client->owner.process_generation != owner.process_generation)
            continue;
        reist_gui_surface_message_t message;
        clear_bytes(&message, sizeof(message));
        message.protocol_version = REIST_GUI_SURFACE_PROTOCOL_VERSION;
        message.message_size = sizeof(message);
        message.type = REIST_GUI_SURFACE_CLOSE;
        message.surface = surface;
        return send_response(client->endpoint, &message);
    }
    return DESKTOP_SURFACE_ESTALE;
}
int desktop_surface_runtime_send_configure(
    desktop_surface_runtime_t *runtime, reist_gui_surface_owner_t owner,
    reist_gui_surface_handle_t surface,
    const reist_gui_surface_configure_t *configure) {
    if (runtime == 0 || configure == 0 || owner.pid == 0U ||
        owner.process_generation == 0U || surface.id == 0U ||
        surface.generation == 0U || configure->serial == 0U ||
        configure->width == 0U || configure->height == 0U)
        return DESKTOP_SURFACE_EINVAL;
    for (uint32_t i = 0U; i < DESKTOP_SURFACE_RUNTIME_CAPACITY; ++i) {
        desktop_surface_runtime_client_t *client = &runtime->clients[i];
        if (client->active != 1U || client->owner.pid != owner.pid ||
            client->owner.process_generation != owner.process_generation)
            continue;
        reist_gui_surface_message_t message;
        clear_bytes(&message, sizeof(message));
        message.protocol_version = REIST_GUI_SURFACE_PROTOCOL_VERSION;
        message.message_size = sizeof(message);
        message.type = REIST_GUI_SURFACE_CONFIGURE;
        message.surface = surface;
        message.serial = configure->serial;
        message.width = configure->width;
        message.height = configure->height;
        return send_response(client->endpoint, &message);
    }
    return DESKTOP_SURFACE_ESTALE;
}
void desktop_surface_runtime_shutdown(desktop_surface_runtime_t *runtime) {
    if (runtime == 0) return;
    for (uint32_t i = 0U; i < DESKTOP_SURFACE_RUNTIME_CAPACITY; ++i) {
        if (runtime->clients[i].active != 0U) {
            (void)x86os_ipc_close(runtime->clients[i].endpoint);
            runtime->clients[i].active = 0U;
        }
    }
}
