#ifndef KERNEL_IPC_H
#define KERNEL_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct Process;

#define IPC_MAX_ENDPOINTS 16U
#define IPC_MAX_CAPABILITIES_PER_PROCESS 8U
#define IPC_QUEUE_DEPTH 4U
#define IPC_MAX_MESSAGE_SIZE 128U
#define IPC_MESSAGE_VERSION 1U

#define IPC_RIGHT_SEND    0x01U
#define IPC_RIGHT_RECEIVE 0x02U
#define IPC_RIGHT_CONTROL 0x04U

#define IPC_INVALID_HANDLE 0U

typedef uint32_t ipc_handle_t;

typedef struct {
    ipc_handle_t handle;
    uint32_t rights;
    bool in_use;
} ipc_capability_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t length;
    uint8_t payload[IPC_MAX_MESSAGE_SIZE];
} ipc_message_t;

void ipc_init(void);
int ipc_create(struct Process *owner, ipc_handle_t *handle);
int ipc_send(struct Process *sender, ipc_handle_t handle,
             const ipc_message_t *message);
int ipc_receive(struct Process *receiver, ipc_handle_t handle,
                ipc_message_t *message);
int ipc_close(struct Process *process, ipc_handle_t handle);
int ipc_inherit(const struct Process *parent, struct Process *child);
void ipc_process_cleanup(int pid, uint32_t generation);

#endif
