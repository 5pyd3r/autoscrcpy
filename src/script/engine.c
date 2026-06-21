#include "engine.h"
#include "bindings.h"
#include "platform/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Chez Scheme embedding API */
#define SCHEME_STATIC 1
#include "scheme.h"

/* Forward declarations */
static DWORD WINAPI script_thread_func(LPVOID arg);
static void scheme_eval_string(const char *code);
static void scheme_load_file(const char *path);
static void scheme_dispatch_msg(script_engine_t *engine, const script_msg_t *msg);
static const char *find_boot_path(void);

bool
script_engine_init(script_engine_t *engine, const char *script_path,
                   const char *eval_expr, script_output_cb_t output_cb,
                   void *output_userdata)
{
    memset(engine, 0, sizeof(*engine));

    if (!script_msg_queue_init(&engine->to_main)) {
        log_error("Failed to init to_main queue");
        return false;
    }

    if (!script_msg_queue_init(&engine->to_scheme)) {
        log_error("Failed to init to_scheme queue");
        script_msg_queue_destroy(&engine->to_main);
        return false;
    }

    if (!script_msg_queue_init(&engine->response_q)) {
        log_error("Failed to init response queue");
        script_msg_queue_destroy(&engine->to_scheme);
        script_msg_queue_destroy(&engine->to_main);
        return false;
    }

    engine->script_path = script_path ? _strdup(script_path) : NULL;
    engine->eval_expr   = eval_expr   ? _strdup(eval_expr)   : NULL;
    engine->output_cb   = output_cb;
    engine->output_userdata = output_userdata;
    engine->running     = false;
    engine->initialized = true;

    log_info("Script engine initialized");
    return true;
}

bool
script_engine_start(script_engine_t *engine)
{
    if (!engine->initialized) {
        log_error("Script engine not initialized");
        return false;
    }

    if (engine->running) {
        log_warn("Script engine already running");
        return false;
    }

    engine->running = true;

    engine->thread = CreateThread(NULL, 0, script_thread_func, engine, 0, NULL);
    if (engine->thread == NULL) {
        log_error("Failed to create script thread (error=%lu)", GetLastError());
        engine->running = false;
        return false;
    }

    log_info("Script engine thread started");
    return true;
}

void
script_engine_stop(script_engine_t *engine)
{
    if (!engine->running) {
        return;
    }

    log_info("Stopping script engine...");
    engine->running = false;

    /* Send shutdown message to wake the thread from recv */
    script_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_SHUTDOWN;
    script_msg_queue_send(&engine->to_scheme, &msg);

    /* Wait for thread to exit */
    if (engine->thread != NULL) {
        DWORD result = WaitForSingleObject(engine->thread, 5000);
        if (result == WAIT_TIMEOUT) {
            log_warn("Script thread did not exit in time, forcing termination");
            TerminateThread(engine->thread, 1);
        }
        CloseHandle(engine->thread);
        engine->thread = NULL;
    }

    log_info("Script engine stopped");
}

void
script_engine_destroy(script_engine_t *engine)
{
    if (!engine->initialized) {
        return;
    }

    script_engine_stop(engine);

    script_msg_queue_destroy(&engine->to_main);
    script_msg_queue_destroy(&engine->to_scheme);
    script_msg_queue_destroy(&engine->response_q);

    free(engine->script_path);
    free(engine->eval_expr);

    memset(engine, 0, sizeof(*engine));
}

bool
script_engine_send(script_engine_t *engine, const script_msg_t *msg)
{
    return script_msg_queue_send(&engine->to_scheme, msg);
}

bool
script_engine_recv(script_engine_t *engine, script_msg_t *msg)
{
    return script_msg_queue_try_recv(&engine->to_main, msg);
}

bool
script_engine_eval(script_engine_t *engine, const char *code)
{
    if (!engine->running || code == NULL) {
        return false;
    }

    script_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_INJECT_TEXT;

    size_t len = strlen(code);
    if (len >= SCRIPT_MSG_MAX_DATA_SIZE) {
        log_error("Eval expression too large (%zu >= %d)", len,
                  SCRIPT_MSG_MAX_DATA_SIZE);
        return false;
    }

    memcpy(msg.data, code, len);
    msg.data_size = (uint32_t)len;

    return script_msg_queue_send(&engine->to_scheme, &msg);
}

bool
script_engine_is_running(const script_engine_t *engine)
{
    return engine->running;
}

/* ===== Chez Scheme integration ===== */

/*
 * Evaluate a Scheme string expression.
 * Uses (eval (read (open-input-string code))) via the top-level environment.
 */
static void
scheme_eval_string(const char *code)
{
    if (code == NULL || code[0] == '\0') return;

    ptr eval_sym  = Sstring_to_symbol("eval");
    ptr read_sym  = Sstring_to_symbol("read");
    ptr ois_sym   = Sstring_to_symbol("open-input-string");

    ptr eval_proc = Stop_level_value(eval_sym);
    ptr read_proc = Stop_level_value(read_sym);
    ptr ois_proc  = Stop_level_value(ois_sym);

    if (eval_proc == Sfalse || read_proc == Sfalse || ois_proc == Sfalse) {
        log_error("Scheme eval/read/open-input-string not available");
        return;
    }

    /* (open-input-string code) */
    ptr port = Scall1(ois_proc, Sstring(code));
    /* (read port) */
    ptr expr = Scall1(read_proc, port);
    /* (eval expr) */
    Scall1(eval_proc, expr);
}

/*
 * Load a Scheme file.
 * Uses (load path) via the top-level environment.
 */
static void
scheme_load_file(const char *path)
{
    if (path == NULL) return;

    ptr load_sym  = Sstring_to_symbol("load");
    ptr load_proc = Stop_level_value(load_sym);

    if (load_proc == Sfalse) {
        log_error("Scheme 'load' not available");
        return;
    }

    log_info("Loading script: %s", path);
    Scall1(load_proc, Sstring(path));
}

/*
 * Dispatch a message from the main thread to Scheme callbacks.
 * Looks up dispatch procedures registered in lib/init.ss.
 */
static void
scheme_dispatch_msg(script_engine_t *engine, const script_msg_t *msg)
{
    const char *handler_name = NULL;

    switch (msg->type) {
    case MSG_EVENT_KEY:    handler_name = "dispatch-key-event"; break;
    case MSG_EVENT_MOUSE:  handler_name = "dispatch-mouse-event"; break;
    case MSG_EVENT_FRAME:  handler_name = "dispatch-frame-event"; break;
    case MSG_EVENT_CONNECTED:    handler_name = "dispatch-connect-event"; break;
    case MSG_EVENT_DISCONNECTED: handler_name = "dispatch-disconnect-event"; break;
    case MSG_INJECT_TEXT:
        /* This is an eval request from script_engine_eval */
        {
            char code[SCRIPT_MSG_MAX_DATA_SIZE + 1];
            memcpy(code, msg->data, msg->data_size);
            code[msg->data_size] = '\0';
            scheme_eval_string(code);
        }
        return;
    default:
        return;
    }

    ptr sym  = Sstring_to_symbol(handler_name);
    ptr proc = Stop_level_value(sym);
    if (proc == Sfalse) {
        /* Handler not registered yet — this is normal before init.ss loads */
        return;
    }

    /* Call the handler with unpacked arguments */
    switch (msg->type) {
    case MSG_EVENT_KEY: {
        if (msg->data_size >= 8) {
            uint32_t vk; int down;
            memcpy(&vk, msg->data, 4);
            memcpy(&down, msg->data + 4, 4);
            Scall2(proc, Sinteger(vk), down ? Strue : Sfalse);
        }
        break;
    }
    case MSG_EVENT_MOUSE: {
        if (msg->data_size >= 16) {
            int32_t x, y; uint32_t buttons, action;
            memcpy(&x, msg->data, 4);
            memcpy(&y, msg->data + 4, 4);
            memcpy(&buttons, msg->data + 8, 4);
            memcpy(&action, msg->data + 12, 4);
            /* Use Sinitframe + Sput_arg for >3 arguments */
            Sinitframe(4);
            Sput_arg(1, Sinteger(x));
            Sput_arg(2, Sinteger(y));
            Sput_arg(3, Sunsigned(buttons));
            Sput_arg(4, Sunsigned(action));
            Scall(proc, 4);
        }
        break;
    }
    case MSG_EVENT_FRAME: {
        if (msg->data_size >= 8) {
            uint32_t w, h;
            memcpy(&w, msg->data, 4);
            memcpy(&h, msg->data + 4, 4);
            Scall2(proc, Sunsigned(w), Sunsigned(h));
        }
        break;
    }
    case MSG_EVENT_CONNECTED:
    case MSG_EVENT_DISCONNECTED:
        Scall0(proc);
        break;
    default:
        break;
    }
}

/*
 * Find the Chez Scheme boot file path (without .boot extension).
 * Sbuild_heap() appends .boot automatically, so we return the stem.
 * Looks for scheme.boot relative to the executable directory.
 * Returns a static buffer — caller must not free.
 */
static const char *
find_boot_path(void)
{
    static char boot_path[MAX_PATH];
    char test_path[MAX_PATH];

    /* Get executable directory */
    char exe_dir[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        log_error("Failed to get executable path");
        return NULL;
    }

    /* Strip filename, keep directory */
    char *last_sep = strrchr(exe_dir, '\\');
    if (!last_sep) last_sep = strrchr(exe_dir, '/');
    if (last_sep) *last_sep = '\0';

    /* Search candidates (with .boot extension for file check) */
    const char *candidates[] = {
        "%s\\scheme",
        "%s\\boot\\pb\\scheme",
        "%s\\..\\boot\\pb\\scheme",
        "%s\\..\\..\\subprojects\\chez-scheme\\boot\\pb\\scheme",
    };

    for (int i = 0; i < 4; i++) {
        snprintf(test_path, MAX_PATH, candidates[i], exe_dir);
        /* Check if the .boot file exists */
        char with_ext[MAX_PATH];
        snprintf(with_ext, MAX_PATH, "%s.boot", test_path);
        if (GetFileAttributesA(with_ext) != INVALID_FILE_ATTRIBUTES) {
            /* Return path WITHOUT .boot (Sbuild_heap appends it) */
            strncpy(boot_path, test_path, MAX_PATH);
            boot_path[MAX_PATH - 1] = '\0';
            return boot_path;
        }
    }

    log_error("scheme.boot not found (searched relative to %s)", exe_dir);
    return NULL;
}

/*
 * Script thread function.
 * Owns the Chez Scheme VM lifecycle.
 */
static DWORD WINAPI
script_thread_func(LPVOID arg)
{
    script_engine_t *engine = (script_engine_t *)arg;

    log_info("Scheme thread running");

    /* Find boot file */
    const char *boot_path = find_boot_path();
    if (!boot_path) {
        log_error("Cannot start Scheme: boot file not found");
        return 1;
    }
    log_info("Using boot file: %s", boot_path);

    /* Initialize Chez Scheme */
    Sscheme_init(NULL);

    /* Register boot file with full path (Sregister_boot_relative_file
     * uses direct path, bypasses search path) */
    char boot_file[MAX_PATH];
    snprintf(boot_file, MAX_PATH, "%s.boot", boot_path);
    Sregister_boot_relative_file(boot_file);
    Sbuild_heap(NULL, NULL);

    /* Register FFI bindings (C functions callable from Scheme) */
    script_bindings_init(&engine->to_main, &engine->response_q);

    /* Load the runtime library (lib/init.ss) */
    scheme_load_file("lib/init.ss");

    /* Load startup script if specified */
    if (engine->script_path) {
        scheme_load_file(engine->script_path);
    }

    /* Execute eval expression if specified */
    if (engine->eval_expr) {
        scheme_eval_string(engine->eval_expr);
    }

    log_info("Scheme VM ready");

    /* Main message loop */
    while (engine->running) {
        script_msg_t msg;
        if (script_msg_queue_recv(&engine->to_scheme, &msg, 100)) {
            if (msg.type == MSG_SHUTDOWN) {
                log_info("Received shutdown signal");
                break;
            }
            scheme_dispatch_msg(engine, &msg);
        }
    }

    /* Cleanup Chez Scheme */
    Sscheme_deinit();

    log_info("Scheme thread exiting");
    return 0;
}
