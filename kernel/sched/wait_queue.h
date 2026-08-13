#ifndef KERNEL_SCHED_WAIT_QUEUE_H
#define KERNEL_SCHED_WAIT_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct wait_queue wait_queue_t;

/* Intrusive queue node.  One task can therefore wait on at most one queue. */
typedef struct wait_queue_node {
    struct wait_queue_node *next;
    wait_queue_t *queue;
    uint64_t key;
} wait_queue_node_t;

struct wait_queue {
    wait_queue_node_t *head;
    wait_queue_node_t *tail;
};

#define WAIT_QUEUE_INIT { NULL, NULL }

/* Raw queue operations.  The caller owns the synchronization boundary; in
 * the kernel scheduler that means IF=0 for the complete operation. */
void wait_queue_init(wait_queue_t *queue);
bool wait_queue_is_empty(const wait_queue_t *queue);
bool wait_queue_push_locked(wait_queue_t *queue, wait_queue_node_t *node);
bool wait_queue_insert_ordered_locked(wait_queue_t *queue,
                                      wait_queue_node_t *node,
                                      uint64_t key);
wait_queue_node_t *wait_queue_pop_locked(wait_queue_t *queue);
bool wait_queue_remove_locked(wait_queue_t *queue, wait_queue_node_t *node);

#endif
