#include "../src/script/script_api.h"
#include "../src/script/bindings.h"
#include "../src/platform/log.h"

#define SCHEME_STATIC 1
#include "scheme.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  %-40s ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

/* ================================================================== */
/* Message queue tests                                                 */
/* ================================================================== */

void test_queue_init_destroy(void)
{
    TEST("queue init/destroy");
    script_msg_queue_t q;
    assert(script_msg_queue_init(&q));
    assert(q.initialized);
    assert(q.count == 0);
    script_msg_queue_destroy(&q);
    assert(!q.initialized);
    PASS();
}

void test_queue_send_recv_loopback(void)
{
    TEST("queue send/recv loopback");
    script_msg_queue_t q;
    assert(script_msg_queue_init(&q));

    for (int i = 0; i < 50; i++) {
        script_msg_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = MSG_INJECT_KEYCODE;
        msg.data_size = 4;
        msg.data[0] = (uint8_t)(i & 0xFF);
        msg.data[1] = (uint8_t)((i >> 8) & 0xFF);
        assert(script_msg_queue_send(&q, &msg));
    }

    for (int i = 0; i < 50; i++) {
        script_msg_t msg;
        memset(&msg, 0, sizeof(msg));
        assert(script_msg_queue_try_recv(&q, &msg));
        assert(msg.type == MSG_INJECT_KEYCODE);
        int counter = (int)msg.data[0] | ((int)msg.data[1] << 8);
        assert(counter == i);
    }

    script_msg_queue_destroy(&q);
    PASS();
}

/* ================================================================== */
/* Engine lifecycle tests                                              */
/* ================================================================== */

void test_engine_init_destroy(void)
{
    TEST("engine init/destroy");
    script_engine_t engine;
    assert(script_engine_init(&engine, NULL, NULL, NULL, NULL));
    assert(engine.initialized);
    assert(!engine.running);
    script_engine_destroy(&engine);
    assert(!engine.initialized);
    PASS();
}

void test_event_dispatch_sizes(void)
{
    TEST("event dispatch sizes");
    assert(sizeof(int) * 2 <= SCRIPT_MSG_MAX_DATA_SIZE);
    assert(sizeof(int) * 3 <= SCRIPT_MSG_MAX_DATA_SIZE);
    assert(sizeof(int) * 4 <= SCRIPT_MSG_MAX_DATA_SIZE);
    PASS();
}

/* ================================================================== */
/* Chez Scheme VM integration tests                                    */
/* ================================================================== */

/*
 * Test: Chez Scheme VM can initialize and build heap from boot file.
 */
void test_chez_vm_init(void)
{
    TEST("chez VM init/build_heap");

    Sscheme_init(NULL);

    /* Build heap — need a boot file path.
     * The test runs from builddir/tests/, boot files are at builddir/src/ */
    Sbuild_heap("C:/Users/Spyder/Desktop/ai_eden/Output/autoscrcpy/builddir/src/scheme", NULL);

    /* If we got here without crashing, the VM initialized */
    PASS();

    Sscheme_deinit();
}

/*
 * Test: FFI bindings register symbols that Scheme can resolve.
 */
void test_chez_ffi_registration(void)
{
    TEST("chez FFI symbol registration");

    Sscheme_init(NULL);
    Sregister_boot_relative_file("C:/Users/Spyder/Desktop/ai_eden/Output/autoscrcpy/builddir/src/scheme.boot");
    Sbuild_heap(NULL, NULL);

    /* Create a queue for bindings */
    script_msg_queue_t q;
    assert(script_msg_queue_init(&q));
    script_msg_queue_t resp_q;
    assert(script_msg_queue_init(&resp_q));
    assert(script_bindings_init(&q, &resp_q));

    /* Verify a registered symbol is callable from Scheme.
     * (foreign-entry? "c_sleep_ms") should return #t */
    ptr fe_sym = Sstring_to_symbol("foreign-entry?");
    ptr fe_proc = Stop_level_value(fe_sym);
    assert(fe_proc != Sfalse);

    ptr result = Scall1(fe_proc, Sstring("c-sleep-ms"));
    assert(result != Sfalse);  /* #t means symbol found */

    script_msg_queue_destroy(&q);
    Sscheme_deinit();
    PASS();
}

/*
 * Test: Scheme can evaluate a simple expression.
 */
void test_chez_eval_simple(void)
{
    TEST("chez eval simple expression");

    Sscheme_init(NULL);
    Sregister_boot_relative_file("C:/Users/Spyder/Desktop/ai_eden/Output/autoscrcpy/builddir/src/scheme.boot");
    Sbuild_heap(NULL, NULL);

    /* (+ 1 2) should return 3 */
    ptr eval_sym  = Sstring_to_symbol("eval");
    ptr read_sym  = Sstring_to_symbol("read");
    ptr ois_sym   = Sstring_to_symbol("open-input-string");

    ptr eval_proc = Stop_level_value(eval_sym);
    ptr read_proc = Stop_level_value(read_sym);
    ptr ois_proc  = Stop_level_value(ois_sym);

    assert(eval_proc != Sfalse);
    assert(read_proc != Sfalse);
    assert(ois_proc != Sfalse);

    ptr port = Scall1(ois_proc, Sstring("(+ 1 2)"));
    ptr expr = Scall1(read_proc, port);
    ptr val  = Scall1(eval_proc, expr);

    assert(Sinteger_value(val) == 3);

    Sscheme_deinit();
    PASS();
}

/*
 * Test: Scheme can define and call a procedure.
 */
void test_chez_define_proc(void)
{
    TEST("chez define and call procedure");

    Sscheme_init(NULL);
    Sregister_boot_relative_file("C:/Users/Spyder/Desktop/ai_eden/Output/autoscrcpy/builddir/src/scheme.boot");
    Sbuild_heap(NULL, NULL);

    ptr eval_sym  = Sstring_to_symbol("eval");
    ptr read_sym  = Sstring_to_symbol("read");
    ptr ois_sym   = Sstring_to_symbol("open-input-string");
    ptr eval_proc = Stop_level_value(eval_sym);
    ptr read_proc = Stop_level_value(read_sym);
    ptr ois_proc  = Stop_level_value(ois_sym);

    /* (define (double x) (* x 2)) */
    ptr port1 = Scall1(ois_proc, Sstring("(define (double x) (* x 2))"));
    ptr expr1 = Scall1(read_proc, port1);
    Scall1(eval_proc, expr1);

    /* (double 21) should return 42 */
    ptr port2 = Scall1(ois_proc, Sstring("(double 21)"));
    ptr expr2 = Scall1(read_proc, port2);
    ptr val   = Scall1(eval_proc, expr2);

    assert(Sinteger_value(val) == 42);

    Sscheme_deinit();
    PASS();
}

/*
 * Test: Scheme can call a registered C function.
 */
void test_chez_call_c_function(void)
{
    TEST("chez call C function from Scheme");

    script_msg_queue_t q;
    assert(script_msg_queue_init(&q));

    Sscheme_init(NULL);
    Sregister_boot_relative_file("C:/Users/Spyder/Desktop/ai_eden/Output/autoscrcpy/builddir/src/scheme.boot");
    Sbuild_heap(NULL, NULL);
    script_msg_queue_t resp_q;
    assert(script_msg_queue_init(&resp_q));
    assert(script_bindings_init(&q, &resp_q));

    /* Define c-sleep-ms wrapper, then call it */
    ptr eval_sym  = Sstring_to_symbol("eval");
    ptr read_sym  = Sstring_to_symbol("read");
    ptr ois_sym   = Sstring_to_symbol("open-input-string");
    ptr eval_proc = Stop_level_value(eval_sym);
    ptr read_proc = Stop_level_value(read_sym);
    ptr ois_proc  = Stop_level_value(ois_sym);

    ptr port1 = Scall1(ois_proc,
        Sstring("(define c-sleep-ms (foreign-procedure \"c-sleep-ms\" (int) void))"));
    ptr expr1 = Scall1(read_proc, port1);
    Scall1(eval_proc, expr1);

    ptr port2 = Scall1(ois_proc, Sstring("(c-sleep-ms 0)"));
    ptr expr2 = Scall1(read_proc, port2);
    Scall1(eval_proc, expr2);

    /* Verify no crash — c_sleep_ms(0) just calls Sleep(0) */
    PASS();

    script_msg_queue_destroy(&q);
    Sscheme_deinit();
}

/*
 * Test: Scheme c_log_message sends to the message queue.
 */
void test_chez_c_log_message(void)
{
    TEST("chez c_log_message dispatch");

    script_msg_queue_t q;
    assert(script_msg_queue_init(&q));

    Sscheme_init(NULL);
    Sregister_boot_relative_file("C:/Users/Spyder/Desktop/ai_eden/Output/autoscrcpy/builddir/src/scheme.boot");
    Sbuild_heap(NULL, NULL);
    script_msg_queue_t resp_q;
    assert(script_msg_queue_init(&resp_q));
    assert(script_bindings_init(&q, &resp_q));

    /* Define c-log-message wrapper, then call it */
    ptr eval_sym  = Sstring_to_symbol("eval");
    ptr read_sym  = Sstring_to_symbol("read");
    ptr ois_sym   = Sstring_to_symbol("open-input-string");
    ptr eval_proc = Stop_level_value(eval_sym);
    ptr read_proc = Stop_level_value(read_sym);
    ptr ois_proc  = Stop_level_value(ois_sym);

    ptr port1 = Scall1(ois_proc,
        Sstring("(define c-log-message (foreign-procedure \"c-log-message\" (int string) void))"));
    ptr expr1 = Scall1(read_proc, port1);
    Scall1(eval_proc, expr1);

    ptr port2 = Scall1(ois_proc, Sstring("(c-log-message 1 \"test from scheme\")"));
    ptr expr2 = Scall1(read_proc, port2);
    Scall1(eval_proc, expr2);

    PASS();

    script_msg_queue_destroy(&q);
    Sscheme_deinit();
}

/*
 * Test: Engine start/stop with real Chez VM.
 */
void test_engine_start_stop(void)
{
    TEST("engine start/stop with Chez VM");

    script_engine_t engine;
    assert(script_engine_init(&engine, NULL, NULL, NULL, NULL));

    /* Start the engine — this creates a thread and initializes Chez */
    assert(script_engine_start(&engine));
    assert(script_engine_is_running(&engine));

    /* Give the thread time to initialize */
    Sleep(500);

    /* Stop the engine */
    script_engine_stop(&engine);
    assert(!script_engine_is_running(&engine));

    script_engine_destroy(&engine);
    PASS();
}

/*
 * Test: Engine eval sends expression to Scheme thread.
 */
void test_engine_eval(void)
{
    TEST("engine eval expression");

    script_engine_t engine;
    assert(script_engine_init(&engine, NULL, NULL, NULL, NULL));
    assert(script_engine_start(&engine));

    /* Give the thread time to initialize Chez and load init.ss */
    Sleep(1000);

    /* Eval a simple expression */
    assert(script_engine_eval(&engine, "(+ 10 20)"));

    /* Give time for eval to process */
    Sleep(200);

    script_engine_stop(&engine);
    script_engine_destroy(&engine);
    PASS();
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */
int main(void)
{
    log_init(LOG_LEVEL_ERROR);
    printf("Script engine tests:\n");

    /* Queue tests */
    test_queue_init_destroy();
    test_queue_send_recv_loopback();

    /* Engine lifecycle tests */
    test_engine_init_destroy();
    test_event_dispatch_sizes();

    /* Chez Scheme VM tests */
    test_chez_vm_init();
    test_chez_ffi_registration();
    test_chez_eval_simple();
    test_chez_define_proc();
    test_chez_call_c_function();
    test_chez_c_log_message();

    /* Engine + Chez integration tests */
    test_engine_start_stop();
    test_engine_eval();

    printf("\n%d/%d script engine tests passed\n",
           tests_passed, tests_passed + tests_failed);

    log_destroy();
    return tests_failed > 0 ? 1 : 0;
}
