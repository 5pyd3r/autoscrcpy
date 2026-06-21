#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "../src/script/message_queue.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(fn) do { \
    printf("Running " #fn "... "); \
    fn(); \
    tests_passed++; \
    printf("passed\n"); \
} while (0)

void test_init_destroy(void) {
    script_msg_queue_t q;
    assert(script_msg_queue_init(&q));
    assert(q.initialized);
    script_msg_queue_destroy(&q);
    assert(!q.initialized);
}

void test_send_recv(void) {
    script_msg_queue_t q;
    assert(script_msg_queue_init(&q));

    script_msg_t send_msg = {0};
    send_msg.type = MSG_INJECT_KEYCODE;
    send_msg.data_size = 8;
    memset(send_msg.data, 0xAB, 8);
    assert(script_msg_queue_send(&q, &send_msg));

    script_msg_t recv_msg = {0};
    assert(script_msg_queue_try_recv(&q, &recv_msg));
    assert(recv_msg.type == MSG_INJECT_KEYCODE);
    assert(recv_msg.data_size == 8);
    for (int i = 0; i < 8; i++) {
        assert(recv_msg.data[i] == 0xAB);
    }

    script_msg_queue_destroy(&q);
}

void test_fifo_order(void) {
    script_msg_queue_t q;
    assert(script_msg_queue_init(&q));

    script_msg_type_t types[] = {
        MSG_INJECT_KEYCODE,
        MSG_INJECT_TEXT,
        MSG_INJECT_TOUCH,
        MSG_INJECT_SCROLL,
        MSG_SET_CLIPBOARD,
    };
    int n = sizeof(types) / sizeof(types[0]);

    for (int i = 0; i < n; i++) {
        script_msg_t msg = {0};
        msg.type = types[i];
        msg.data_size = 4;
        msg.data[0] = (uint8_t)i;
        assert(script_msg_queue_send(&q, &msg));
    }

    for (int i = 0; i < n; i++) {
        script_msg_t msg = {0};
        assert(script_msg_queue_try_recv(&q, &msg));
        assert(msg.type == types[i]);
        assert(msg.data[0] == (uint8_t)i);
    }

    script_msg_queue_destroy(&q);
}

void test_empty_recv(void) {
    script_msg_queue_t q;
    assert(script_msg_queue_init(&q));

    script_msg_t msg = {0};
    assert(!script_msg_queue_try_recv(&q, &msg));

    script_msg_queue_destroy(&q);
}

void test_fill_and_overflow(void) {
    script_msg_queue_t q;
    assert(script_msg_queue_init(&q));

    for (int i = 0; i < SCRIPT_MSG_QUEUE_CAPACITY; i++) {
        script_msg_t msg = {0};
        msg.type = MSG_INJECT_KEYCODE;
        msg.data_size = 1;
        msg.data[0] = (uint8_t)(i & 0xFF);
        assert(script_msg_queue_send(&q, &msg));
    }

    /* Next send should fail */
    script_msg_t overflow_msg = {0};
    overflow_msg.type = MSG_SHUTDOWN;
    assert(!script_msg_queue_send(&q, &overflow_msg));

    script_msg_queue_destroy(&q);
}

void test_drain(void) {
    script_msg_queue_t q;
    assert(script_msg_queue_init(&q));

    for (int i = 0; i < 10; i++) {
        script_msg_t msg = {0};
        msg.type = MSG_INJECT_KEYCODE;
        assert(script_msg_queue_send(&q, &msg));
    }

    script_msg_queue_drain(&q);
    assert(q.count == 0);

    script_msg_t msg = {0};
    assert(!script_msg_queue_try_recv(&q, &msg));

    script_msg_queue_destroy(&q);
}

int main(void) {
    RUN_TEST(test_init_destroy);
    RUN_TEST(test_send_recv);
    RUN_TEST(test_fifo_order);
    RUN_TEST(test_empty_recv);
    RUN_TEST(test_fill_and_overflow);
    RUN_TEST(test_drain);

    printf("\n%d/%d message queue tests passed\n", tests_passed, tests_passed + tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
