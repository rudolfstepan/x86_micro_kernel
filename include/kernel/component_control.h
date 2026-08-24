/**
 * @file include/kernel/component_control.h
 * @brief Einheitlicher Down/Up-Vertrag für administrierbare Systemkomponenten.
 *
 * Layer: Ring-0 public subsystem interface.
 * Contract: Anfragen tragen Prozessgeneration, Komponenten-ID, Aktion und monotone Deadline.
 * Safety: Nur registrierte, autorisierte Übergänge werden ausgeführt; Timeout bleibt fail-closed.
 */
#ifndef KERNEL_COMPONENT_CONTROL_H
#define KERNEL_COMPONENT_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include "include/reist/abi/syscall.h"

#define COMPONENT_CONTROL_SYSCALL REIST_SYS_COMPONENT_CONTROL
#define COMPONENT_CONTROL_ABI_VERSION 1U
#define COMPONENT_CONTROL_MAX_COMPONENTS 7U
#define COMPONENT_CONTROL_NAME_CAPACITY 24U
#define COMPONENT_CONTROL_TIMEOUT_DEFAULT_MS 1000U
#define COMPONENT_CONTROL_TIMEOUT_MAX_MS 5000U

#define COMPONENT_EPROTECTED (-1003)
#define COMPONENT_EDEPENDENCY (-1004)
#define COMPONENT_ESTATE (-1005)

enum {
    COMPONENT_ID_SCHEDULER = 0,
    COMPONENT_ID_CLOCK = 1,
    COMPONENT_ID_INTERRUPTS = 2,
    COMPONENT_ID_ROOT_STORAGE = 3,
    COMPONENT_ID_NETWORK_DRIVER = 4,
    COMPONENT_ID_STORAGE_SERVICE = 5,
    COMPONENT_ID_NETWORK_SERVICE = 6,
};

enum {
    COMPONENT_COMMAND_STATUS = 0,
    COMPONENT_COMMAND_DOWN = 1,
    COMPONENT_COMMAND_UP = 2,
    COMPONENT_COMMAND_RESTART = 3,
};

enum {
    COMPONENT_STATE_READY = 1,
    COMPONENT_STATE_QUIESCING = 2,
    COMPONENT_STATE_DOWN = 3,
    COMPONENT_STATE_STARTING = 4,
    COMPONENT_STATE_FAILED = 5,
};

#define COMPONENT_FLAG_MANAGEABLE (1U << 0)
#define COMPONENT_FLAG_PROTECTED  (1U << 1)
#define COMPONENT_FLAG_DRIVER     (1U << 2)
#define COMPONENT_FLAG_SERVICE    (1U << 3)
#define COMPONENT_FLAG_FENCED     (1U << 4)

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t command;
    uint32_t component;
    uint32_t expected_generation;
    uint32_t timeout_ms;
} component_control_request_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t component;
    uint32_t state;
    uint32_t generation;
    uint32_t flags;
    uint32_t dependency_mask;
    int32_t last_error;
    char name[COMPONENT_CONTROL_NAME_CAPACITY];
} component_control_result_t;

bool component_control_init(void);
void component_control_poll(uint64_t now_ms);
int component_control_execute(int pid, uint32_t process_generation,
                              const component_control_request_t *request,
                              component_control_result_t *result,
                              uint64_t now_ms);
void component_control_process_cleanup(int pid, uint32_t process_generation);

#endif
