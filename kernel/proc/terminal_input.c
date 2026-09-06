#include "kernel/proc/terminal_input.h"
#include "drivers/char/kb.h"
#include "include/kernel/panic.h"
#include "arch/x86/include/interrupt.h"
#include "lib/libc/string.h"

spinlock_t terminal_input_lock = SPINLOCK_INIT;
typedef struct { int pid; uint32_t generation; } terminal_identity_t;
static terminal_identity_t foreground[TERMINAL_INPUT_DEPTH];
static uint32_t foreground_count;
static bool console_attached;

bool terminal_input_admitted_locked(int pid, uint32_t generation) {
    KASSERT(spinlock_is_owned_by_current(&terminal_input_lock));
    if (foreground_count == 0U) return pid == 0 && generation == 0U;
    const terminal_identity_t *owner = &foreground[foreground_count - 1U];
    return pid == owner->pid && generation == owner->generation;
}

static int foreground_index(int pid, uint32_t generation) {
    for (uint32_t i = 0U; i < foreground_count; ++i)
        if (foreground[i].pid == pid && foreground[i].generation == generation)
            return (int)i;
    return -1;
}

int terminal_input_control_locked(Process *caller, Process *target,
                                   const reist_terminal_input_request_t *request) {
    KASSERT(process_table_lock_is_owned());
    if (caller == NULL || request == NULL ||
        request->version != REIST_TERMINAL_INPUT_VERSION ||
        request->struct_size != sizeof(*request) || request->reserved != 0U ||
        request->operation < REIST_TERMINAL_ATTACH_CONSOLE ||
        request->operation > REIST_TERMINAL_CHECK) return -22;
    if (request->operation == REIST_TERMINAL_TRANSFER) {
        if (target == NULL || request->target_pid != target->pid ||
            request->target_generation != target->generation) return -116;
    } else if (request->target_pid != 0 || request->target_generation != 0U) {
        return -22;
    }
    uint32_t flags = spinlock_acquire_irq(&terminal_input_lock);
    int index = foreground_index(caller->pid, caller->generation);
    bool current = index >= 0 && (uint32_t)index + 1U == foreground_count;
    bool changed = false;
    int result = -13;
    switch (request->operation) {
    case REIST_TERMINAL_ATTACH_CONSOLE:
        if (console_attached && index == 0) { result = 0; break; }
        /* The boot/rescue shell image is already a protected launch contract.
         * A spawned shell can only receive a foreground delegation. */
        if (caller->parent_pid != 0 ||
            strcmp(caller->image_path, "/bin/shell.prg") != 0) break;
        if (foreground_count != 0U) { result = -16; break; }
        foreground[0] = (terminal_identity_t){caller->pid, caller->generation};
        foreground_count = 1U;
        console_attached = true;
        changed = true;
        result = 0;
        break;
    case REIST_TERMINAL_TRANSFER: {
        bool child = target->parent_pid == caller->pid &&
                     target->parent_generation == caller->generation;
        bool service = console_attached && index == 0 &&
                       target->domain_profile.kind == PROCESS_DOMAIN_COMPOSITOR;
        if (!child && !service) break;
        int target_index = foreground_index(target->pid, target->generation);
        if (target_index >= 0) {
            result = target_index == index + 1 &&
                     (uint32_t)target_index + 1U == foreground_count ? 0 : -16;
            break;
        }
        if (!current) break;
        if (foreground_count == TERMINAL_INPUT_DEPTH) { result = -28; break; }
        foreground[foreground_count++] =
            (terminal_identity_t){target->pid, target->generation};
        changed = true;
        result = 0;
        break;
    }
    case REIST_TERMINAL_RELEASE:
        if (index < 0) { result = 0; break; } /* idempotent, no foreign revoke */
        if (!current || (console_attached && index == 0)) break;
        --foreground_count;
        changed = true;
        result = 0;
        break;
    case REIST_TERMINAL_ACQUIRE_SERVICE:
        if (caller->domain_profile.kind != PROCESS_DOMAIN_COMPOSITOR) break;
        if (current) { result = 0; break; }
        if (!console_attached || foreground_count != 1U) { result = -16; break; }
        foreground[foreground_count++] =
            (terminal_identity_t){caller->pid, caller->generation};
        changed = true;
        result = 0;
        break;
    case REIST_TERMINAL_CHECK:
        result = current ? 0 : -11;
        break;
    }
    if (changed) kb_input_owner_changed_locked();
    spinlock_release(&terminal_input_lock);
    if (changed) kb_input_owner_wake();
    irq_restore(flags);
    return result;
}

void terminal_input_process_cleanup(int pid, uint32_t generation) {
    KASSERT(process_table_lock_is_owned());
    uint32_t flags = spinlock_acquire_irq(&terminal_input_lock);
    int index = foreground_index(pid, generation);
    if (index >= 0) {
        foreground_count = (uint32_t)index;
        if (index == 0) console_attached = false;
        kb_input_owner_changed_locked();
    }
    spinlock_release(&terminal_input_lock);
    if (index >= 0) kb_input_owner_wake();
    irq_restore(flags);
}
