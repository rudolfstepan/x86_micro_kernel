#include "kernel/sched/wait_queue.h"

void wait_queue_init(wait_queue_t *queue) {
    if (queue == NULL) return;
    queue->head = NULL;
    queue->tail = NULL;
}

bool wait_queue_is_empty(const wait_queue_t *queue) {
    return queue == NULL || queue->head == NULL;
}

bool wait_queue_push_locked(wait_queue_t *queue, wait_queue_node_t *node) {
    if (queue == NULL || node == NULL || node->queue != NULL) return false;
    node->next = NULL;
    node->queue = queue;
    if (queue->tail == NULL) {
        queue->head = node;
    } else {
        queue->tail->next = node;
    }
    queue->tail = node;
    return true;
}

bool wait_queue_insert_ordered_locked(wait_queue_t *queue,
                                      wait_queue_node_t *node,
                                      uint64_t key) {
    if (queue == NULL || node == NULL || node->queue != NULL) return false;
    node->key = key;
    node->queue = queue;

    wait_queue_node_t *previous = NULL;
    wait_queue_node_t *current = queue->head;
    /* Insert after equal keys so tasks sharing a deadline retain FIFO order. */
    while (current != NULL && current->key <= key) {
        previous = current;
        current = current->next;
    }
    node->next = current;
    if (previous == NULL) {
        queue->head = node;
    } else {
        previous->next = node;
    }
    if (current == NULL) queue->tail = node;
    return true;
}

wait_queue_node_t *wait_queue_pop_locked(wait_queue_t *queue) {
    if (queue == NULL || queue->head == NULL) return NULL;
    wait_queue_node_t *node = queue->head;
    queue->head = node->next;
    if (queue->head == NULL) queue->tail = NULL;
    node->next = NULL;
    node->queue = NULL;
    return node;
}

bool wait_queue_remove_locked(wait_queue_t *queue, wait_queue_node_t *node) {
    if (queue == NULL || node == NULL || node->queue != queue) return false;
    wait_queue_node_t *previous = NULL;
    wait_queue_node_t *current = queue->head;
    while (current != NULL && current != node) {
        previous = current;
        current = current->next;
    }
    if (current == NULL) return false;
    if (previous == NULL) {
        queue->head = current->next;
    } else {
        previous->next = current->next;
    }
    if (queue->tail == current) queue->tail = previous;
    current->next = NULL;
    current->queue = NULL;
    return true;
}
