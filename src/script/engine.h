#ifndef SCRIPT_ENGINE_H
#define SCRIPT_ENGINE_H

#include "message_queue.h"
#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

typedef struct script_engine script_engine_t;

typedef void (*script_output_cb_t)(const char *text, bool is_error, void *userdata);

struct script_engine {
    script_msg_queue_t to_main;      /* Scheme -> Main (commands + queries) */
    script_msg_queue_t to_scheme;    /* Main -> Scheme (events) */
    script_msg_queue_t response_q;   /* Main -> Scheme (query responses) */
    HANDLE thread;
    volatile bool running;
    volatile bool initialized;
    script_output_cb_t output_cb;
    void *output_userdata;
    char *script_path;
    char *eval_expr;
};

bool script_engine_init(script_engine_t *engine, const char *script_path,
                        const char *eval_expr, script_output_cb_t output_cb,
                        void *output_userdata);
bool script_engine_start(script_engine_t *engine);
void script_engine_stop(script_engine_t *engine);
void script_engine_destroy(script_engine_t *engine);
bool script_engine_send(script_engine_t *engine, const script_msg_t *msg);
bool script_engine_recv(script_engine_t *engine, script_msg_t *msg);
bool script_engine_eval(script_engine_t *engine, const char *code);
bool script_engine_is_running(const script_engine_t *engine);

#endif /* SCRIPT_ENGINE_H */
