/**
 * @file include/kernel/ipc.h
 * @brief Definiert die versionierte Kernel-IPC-API und ihre Kapazitätsgrenzen.
 *
 * Layer: Ring-0 public subsystem interface.
 * Contract: Nachrichten tragen Version, Strukturgröße und Nutzlänge; Rechte sind
 *           explizite Bitmasken und werden nie aus Handlebesitz abgeleitet.
 * Safety: Alle öffentlichen Größen sind hart begrenzt und ungültige Handles
 *         werden ohne Queue- oder Ownership-Änderung abgewiesen.
 */
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
#define IPC_BULK_MAX_MESSAGE_SIZE 2048U
#define IPC_BULK_MESSAGE_VERSION 2U
#define IPC_DEFAULT_TIMEOUT_MS 1000U
#define IPC_RESOURCE_STATS_VERSION 1U

#define IPC_RIGHT_SEND    0x01U
#define IPC_RIGHT_RECEIVE 0x02U
#define IPC_RIGHT_CONTROL 0x04U

#define IPC_INVALID_HANDLE 0U
#define IPC_EINTEGRITY (-84)
#define IPC_EPIPE (-32)

typedef enum {
    IPC_FAULT_ENDPOINT = 0,
    IPC_FAULT_CAPABILITY = 1,
    IPC_FAULT_MESSAGE = 2,
    IPC_FAULT_BULK_METADATA = 3,
    IPC_FAULT_BULK_PAYLOAD = 4,
} ipc_fault_target_t;

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

/**
 * Append-only large-message ABI. Each endpoint has exactly one rendezvous
 * slot, separate from the four-entry version-1 queue. The kernel validates a
 * protected header and CRC32 before exposing any payload to the receiver.
 */
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t length;
    uint8_t payload[IPC_BULK_MAX_MESSAGE_SIZE];
} ipc_bulk_message_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t active_endpoints;
    uint32_t endpoint_high_water;
    uint32_t active_capabilities;
    uint32_t capability_high_water;
    uint32_t queued_messages;
    uint32_t message_high_water;
    uint32_t capacity_rejections;
} ipc_resource_stats_t;

void ipc_init(void);
int ipc_create(struct Process *owner, ipc_handle_t *handle);
int ipc_send(struct Process *sender, ipc_handle_t handle,
             const ipc_message_t *message);
int ipc_send_timeout(struct Process *sender, ipc_handle_t handle,
                     const ipc_message_t *message, uint32_t timeout_ms);
int ipc_send_bulk_timeout(struct Process *sender, ipc_handle_t handle,
                          const ipc_bulk_message_t *message,
                          uint32_t timeout_ms);
/* Nonblocking trusted ingress.  The active peer identity is used as the
 * sender so only the endpoint owner can receive the injected message. */
int ipc_send_external_from_peer(int owner_pid, uint32_t owner_generation,
                                ipc_handle_t handle,
                                const ipc_message_t *message);
/* Nonblocking kernel-originated control ingress.  The reserved sender
 * identity (0, 0) is receivable only by the exact endpoint owner and does
 * not manufacture or require a userspace peer capability. */
int ipc_send_kernel_to_owner(int owner_pid, uint32_t owner_generation,
                             ipc_handle_t handle,
                             const ipc_message_t *message);
/** Validate that one live identity owns an endpoint with all requested rights. */
int ipc_capability_validate_owner(int owner_pid, uint32_t owner_generation,
                                  ipc_handle_t handle,
                                  uint32_t required_rights);
/**
 * Validate an exact live endpoint owner and prove that no peer capability or
 * queued message remains. Used before reusing a session endpoint.
 */
int ipc_endpoint_validate_quiescent_owner(
    int owner_pid, uint32_t owner_generation, ipc_handle_t handle);
int ipc_receive(struct Process *receiver, ipc_handle_t handle,
                ipc_message_t *message);
int ipc_receive_timeout(struct Process *receiver, ipc_handle_t handle,
                        ipc_message_t *message, uint32_t timeout_ms);
/** Receive either a version-1 queued message or one version-2 bulk message. */
int ipc_receive_bulk_timeout(struct Process *receiver, ipc_handle_t handle,
                             ipc_bulk_message_t *message,
                             uint32_t timeout_ms);
int ipc_close(struct Process *process, ipc_handle_t handle);
int ipc_release(struct Process *process, ipc_handle_t handle);
int ipc_delegate(struct Process *source, ipc_handle_t handle,
                 struct Process *target, uint32_t rights);
void ipc_process_cleanup(int pid, uint32_t generation);
int ipc_fault_inject(ipc_fault_target_t target, size_t object_index,
                     size_t copy_index, size_t word_index, uint32_t bit_mask);
uint32_t ipc_integrity_correction_count(void);
int ipc_resource_stats(ipc_resource_stats_t *stats_out);

#endif
