#include "kernel/sched/wait_queue.h"

#include <stdint.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

typedef struct {
    unsigned id;
    wait_queue_node_t node;
} test_item_t;

static test_item_t make_item(unsigned id) {
    test_item_t item = {0};
    item.id = id;
    return item;
}

static int test_initialization_and_fifo(void) {
    wait_queue_t queue = WAIT_QUEUE_INIT;
    wait_queue_t other = WAIT_QUEUE_INIT;
    test_item_t first = make_item(1);
    test_item_t second = make_item(2);
    test_item_t third = make_item(3);

    CHECK(wait_queue_is_empty(&queue));
    CHECK(queue.head == NULL && queue.tail == NULL);
    CHECK(wait_queue_push_locked(&queue, &first.node));
    CHECK(wait_queue_push_locked(&queue, &second.node));
    CHECK(wait_queue_push_locked(&queue, &third.node));
    CHECK(!wait_queue_is_empty(&queue));
    CHECK(queue.head == &first.node && queue.tail == &third.node);
    CHECK(first.node.next == &second.node);
    CHECK(second.node.next == &third.node);
    CHECK(third.node.next == NULL);

    /* An intrusive node may belong to only one queue and occur only once. */
    CHECK(!wait_queue_push_locked(&queue, &second.node));
    CHECK(!wait_queue_push_locked(&other, &second.node));

    CHECK(wait_queue_pop_locked(&queue) == &first.node);
    CHECK(first.node.queue == NULL && first.node.next == NULL);
    CHECK(wait_queue_pop_locked(&queue) == &second.node);
    CHECK(second.node.queue == NULL && second.node.next == NULL);
    CHECK(wait_queue_pop_locked(&queue) == &third.node);
    CHECK(third.node.queue == NULL && third.node.next == NULL);
    CHECK(wait_queue_pop_locked(&queue) == NULL);
    CHECK(wait_queue_is_empty(&queue));
    CHECK(queue.head == NULL && queue.tail == NULL);

    /* A detached node is reusable. */
    CHECK(wait_queue_push_locked(&other, &second.node));
    CHECK(wait_queue_pop_locked(&other) == &second.node);
    return 0;
}

static int test_stable_deadline_order(void) {
    wait_queue_t queue;
    wait_queue_init(&queue);
    test_item_t nine_first = make_item(1);
    test_item_t three = make_item(2);
    test_item_t nine_second = make_item(3);
    test_item_t maximum = make_item(4);
    test_item_t zero = make_item(5);
    test_item_t nine_third = make_item(6);

    CHECK(wait_queue_insert_ordered_locked(&queue, &nine_first.node, 9));
    CHECK(wait_queue_insert_ordered_locked(&queue, &three.node, 3));
    CHECK(wait_queue_insert_ordered_locked(&queue, &nine_second.node, 9));
    CHECK(wait_queue_insert_ordered_locked(
        &queue, &maximum.node, UINT64_MAX));
    CHECK(wait_queue_insert_ordered_locked(&queue, &zero.node, 0));
    CHECK(wait_queue_insert_ordered_locked(&queue, &nine_third.node, 9));

    wait_queue_node_t *expected[] = {
        &zero.node,
        &three.node,
        &nine_first.node,
        &nine_second.node,
        &nine_third.node,
        &maximum.node,
    };
    uint64_t previous_key = 0;
    for (unsigned index = 0; index < sizeof(expected) / sizeof(expected[0]);
         ++index) {
        wait_queue_node_t *node = wait_queue_pop_locked(&queue);
        CHECK(node == expected[index]);
        CHECK(index == 0 || node->key >= previous_key);
        previous_key = node->key;
        CHECK(node->queue == NULL && node->next == NULL);
    }
    CHECK(wait_queue_is_empty(&queue));
    CHECK(queue.tail == NULL);
    return 0;
}

static int test_remove_head_middle_and_tail(void) {
    wait_queue_t queue = WAIT_QUEUE_INIT;
    wait_queue_t other = WAIT_QUEUE_INIT;
    test_item_t first = make_item(1);
    test_item_t second = make_item(2);
    test_item_t third = make_item(3);
    test_item_t fourth = make_item(4);

    CHECK(wait_queue_push_locked(&queue, &first.node));
    CHECK(wait_queue_push_locked(&queue, &second.node));
    CHECK(wait_queue_push_locked(&queue, &third.node));
    CHECK(wait_queue_push_locked(&queue, &fourth.node));

    CHECK(!wait_queue_remove_locked(&other, &second.node));
    CHECK(wait_queue_remove_locked(&queue, &second.node));
    CHECK(second.node.queue == NULL && second.node.next == NULL);
    CHECK(first.node.next == &third.node);
    CHECK(!wait_queue_remove_locked(&queue, &second.node));

    CHECK(wait_queue_remove_locked(&queue, &first.node));
    CHECK(queue.head == &third.node && queue.tail == &fourth.node);
    CHECK(first.node.queue == NULL && first.node.next == NULL);

    CHECK(wait_queue_remove_locked(&queue, &fourth.node));
    CHECK(queue.head == &third.node && queue.tail == &third.node);
    CHECK(fourth.node.queue == NULL && fourth.node.next == NULL);

    CHECK(wait_queue_remove_locked(&queue, &third.node));
    CHECK(wait_queue_is_empty(&queue));
    CHECK(queue.head == NULL && queue.tail == NULL);

    CHECK(wait_queue_push_locked(&other, &second.node));
    CHECK(wait_queue_push_locked(&other, &fourth.node));
    CHECK(wait_queue_pop_locked(&other) == &second.node);
    CHECK(wait_queue_pop_locked(&other) == &fourth.node);
    return 0;
}

int main(void) {
    int result = test_initialization_and_fifo();
    if (result != 0) return result;
    result = test_stable_deadline_order();
    if (result != 0) return result;
    return test_remove_head_middle_and_tail();
}
