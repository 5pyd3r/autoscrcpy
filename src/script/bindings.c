#include "bindings.h"
#include "platform/log.h"

#include <string.h>
#include <windows.h>

/* Chez Scheme embedding API */
#define SCHEME_STATIC 1
#include "scheme.h"

/* Queue for sending messages from Scheme thread to main thread. */
static script_msg_queue_t *g_to_main = NULL;

/* Queue for receiving query responses from main thread. */
static script_msg_queue_t *g_response_q = NULL;

/* ------------------------------------------------------------------ */
/* Helper: send a message if the queue is available                    */
/* ------------------------------------------------------------------ */
static bool send_msg(const script_msg_t *msg)
{
    if (!g_to_main) {
        log_warn("script bindings: g_to_main is NULL, message dropped");
        return false;
    }
    return script_msg_queue_send(g_to_main, msg);
}

/* ------------------------------------------------------------------ */
/* Helper: send a query and wait for synchronous response              */
/* ------------------------------------------------------------------ */
static bool query_sync(script_msg_type_t qtype, const script_msg_t *qmsg,
                        script_msg_t *response, uint32_t timeout_ms)
{
    if (!g_to_main || !g_response_q) {
        log_warn("script bindings: query queues not ready");
        return false;
    }

    /* Send query to main thread */
    if (!script_msg_queue_send(g_to_main, qmsg)) {
        log_warn("script bindings: failed to send query %d", qtype);
        return false;
    }

    /* Wait for response */
    if (!script_msg_queue_recv(g_response_q, response, timeout_ms)) {
        log_warn("script bindings: query %d timed out", qtype);
        return false;
    }

    return true;
}

/* ================================================================== */
/* Control commands (fire-and-forget)                                  */
/* ================================================================== */

static void c_inject_keycode(int keycode, int down)
{
    script_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_INJECT_KEYCODE;
    msg.data_size = 8;
    memcpy(msg.data,     &keycode, 4);
    memcpy(msg.data + 4, &down,    4);
    send_msg(&msg);
}

static void c_inject_text(const char *text)
{
    if (!text) return;
    script_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_INJECT_TEXT;
    size_t len = strlen(text);
    if (len > SCRIPT_MSG_MAX_DATA_SIZE - 1)
        len = SCRIPT_MSG_MAX_DATA_SIZE - 1;
    memcpy(msg.data, text, len);
    msg.data[len] = '\0';
    msg.data_size = (uint32_t)(len + 1);
    send_msg(&msg);
}

static void c_inject_touch(int x, int y, int action)
{
    script_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_INJECT_TOUCH;
    msg.data_size = 12;
    memcpy(msg.data,     &x,      4);
    memcpy(msg.data + 4, &y,      4);
    memcpy(msg.data + 8, &action, 4);
    send_msg(&msg);
}

static void c_inject_scroll(int x, int y, int dx, int dy)
{
    script_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_INJECT_SCROLL;
    msg.data_size = 16;
    memcpy(msg.data,      &x,  4);
    memcpy(msg.data + 4,  &y,  4);
    memcpy(msg.data + 8,  &dx, 4);
    memcpy(msg.data + 12, &dy, 4);
    send_msg(&msg);
}

static void c_set_clipboard(const char *text)
{
    if (!text) return;
    script_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_SET_CLIPBOARD;
    size_t len = strlen(text);
    if (len > SCRIPT_MSG_MAX_DATA_SIZE - 1)
        len = SCRIPT_MSG_MAX_DATA_SIZE - 1;
    memcpy(msg.data, text, len);
    msg.data[len] = '\0';
    msg.data_size = (uint32_t)(len + 1);
    send_msg(&msg);
}

static void c_expand_notification(void)
{
    script_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_EXPAND_NOTIFICATION;
    msg.data_size = 0;
    send_msg(&msg);
}

static void c_collapse_panels(void)
{
    script_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_COLLAPSE_PANELS;
    msg.data_size = 0;
    send_msg(&msg);
}

static void c_set_display_power(int on)
{
    script_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_SET_DISPLAY_POWER;
    msg.data_size = 4;
    memcpy(msg.data, &on, 4);
    send_msg(&msg);
}

static void c_rotate_device(void)
{
    script_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_ROTATE_DEVICE;
    msg.data_size = 0;
    send_msg(&msg);
}

static void c_start_app(const char *package)
{
    if (!package) return;
    script_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_START_APP;
    size_t len = strlen(package);
    if (len > SCRIPT_MSG_MAX_DATA_SIZE - 1)
        len = SCRIPT_MSG_MAX_DATA_SIZE - 1;
    memcpy(msg.data, package, len);
    msg.data[len] = '\0';
    msg.data_size = (uint32_t)(len + 1);
    send_msg(&msg);
}

static void c_sleep_ms(int ms)
{
    if (ms < 0) ms = 0;
    Sleep((DWORD)ms);
}

static void c_log_message(int level, const char *msg)
{
    if (!msg) return;
    switch (level) {
    case LOG_LEVEL_VERBOSE: log_verbose("%s", msg); break;
    case LOG_LEVEL_DEBUG:   log_debug("%s", msg);   break;
    case LOG_LEVEL_INFO:    log_info("%s", msg);    break;
    case LOG_LEVEL_WARN:    log_warn("%s", msg);    break;
    case LOG_LEVEL_ERROR:   log_error("%s", msg);   break;
    default:                log_info("%s", msg);    break;
    }
}

/* ================================================================== */
/* Synchronous query functions                                         */
/* ================================================================== */

/*
 * c_query_device_info() -> '(width height name connected)
 * Returns a Scheme list with device information.
 */
static ptr c_query_device_info(void)
{
    script_msg_t qmsg, resp;
    memset(&qmsg, 0, sizeof(qmsg));
    qmsg.type = MSG_QUERY_DEVICE_INFO;

    if (!query_sync(MSG_QUERY_DEVICE_INFO, &qmsg, &resp, 5000)) {
        return Sfalse;
    }

    if (resp.type != MSG_RESPONSE_DEVICE_INFO || resp.data_size < 76) {
        return Sfalse;
    }

    uint32_t w, h, connected;
    memcpy(&w, resp.data, 4);
    memcpy(&h, resp.data + 4, 4);
    char name[64];
    memcpy(name, resp.data + 8, 64);
    name[63] = '\0';
    memcpy(&connected, resp.data + 72, 4);

    return Scons(Sunsigned(w),
           Scons(Sunsigned(h),
           Scons(Sstring(name),
           Scons(connected ? Strue : Sfalse, Snil))));
}

/*
 * c_query_video_size() -> '(width . height)
 */
static ptr c_query_video_size(void)
{
    script_msg_t qmsg, resp;
    memset(&qmsg, 0, sizeof(qmsg));
    qmsg.type = MSG_QUERY_VIDEO_SIZE;

    if (!query_sync(MSG_QUERY_VIDEO_SIZE, &qmsg, &resp, 5000)) {
        return Sfalse;
    }

    if (resp.type != MSG_RESPONSE_VIDEO_SIZE || resp.data_size < 8) {
        return Sfalse;
    }

    uint32_t w, h;
    memcpy(&w, resp.data, 4);
    memcpy(&h, resp.data + 4, 4);

    return Scons(Sunsigned(w), Sunsigned(h));
}

/*
 * c_query_window_size() -> '(width . height)
 */
static ptr c_query_window_size(void)
{
    script_msg_t qmsg, resp;
    memset(&qmsg, 0, sizeof(qmsg));
    qmsg.type = MSG_QUERY_WINDOW_SIZE;

    if (!query_sync(MSG_QUERY_WINDOW_SIZE, &qmsg, &resp, 5000)) {
        return Sfalse;
    }

    if (resp.type != MSG_RESPONSE_WINDOW_SIZE || resp.data_size < 8) {
        return Sfalse;
    }

    uint32_t w, h;
    memcpy(&w, resp.data, 4);
    memcpy(&h, resp.data + 4, 4);

    return Scons(Sunsigned(w), Sunsigned(h));
}

/*
 * c_query_clipboard() -> string or #f
 */
static ptr c_query_clipboard(void)
{
    script_msg_t qmsg, resp;
    memset(&qmsg, 0, sizeof(qmsg));
    qmsg.type = MSG_QUERY_CLIPBOARD;

    if (!query_sync(MSG_QUERY_CLIPBOARD, &qmsg, &resp, 5000)) {
        return Sfalse;
    }

    if (resp.type != MSG_RESPONSE_CLIPBOARD) {
        return Sfalse;
    }

    return Sstring((const char *)resp.data);
}

/*
 * c_query_frame_capture() -> bytevector or #f
 * Returns NV12 frame data as a Scheme bytevector.
 */
static ptr c_query_frame_capture(void)
{
    script_msg_t qmsg, resp;
    memset(&qmsg, 0, sizeof(qmsg));
    qmsg.type = MSG_QUERY_FRAME_CAPTURE;

    if (!query_sync(MSG_QUERY_FRAME_CAPTURE, &qmsg, &resp, 5000)) {
        return Sfalse;
    }

    if (resp.type != MSG_RESPONSE_FRAME_CAPTURE || resp.data_size < 12) {
        return Sfalse;
    }

    uint32_t w, h, data_size;
    memcpy(&w, resp.data, 4);
    memcpy(&h, resp.data + 4, 4);
    memcpy(&data_size, resp.data + 8, 4);

    if (data_size == 0 || resp.data_size < 12 + data_size) {
        return Sfalse;
    }

    /* Create bytevector with NV12 data */
    ptr bv = Smake_bytevector((iptr)data_size, 0);
    memcpy(TO_VOIDP((uptr)bv + 9), resp.data + 12, data_size);

    return bv;
}

/* ================================================================== */
/* Fill response helpers (called from main thread)                     */
/* ================================================================== */

void script_fill_device_info(script_msg_t *resp, uint32_t w, uint32_t h,
                             const char *name, bool connected)
{
    memset(resp, 0, sizeof(*resp));
    resp->type = MSG_RESPONSE_DEVICE_INFO;
    resp->data_size = 76;
    memcpy(resp->data, &w, 4);
    memcpy(resp->data + 4, &h, 4);
    if (name) {
        size_t len = strlen(name);
        if (len > 63) len = 63;
        memcpy(resp->data + 8, name, len);
        resp->data[8 + len] = '\0';
    }
    uint32_t c = connected ? 1 : 0;
    memcpy(resp->data + 72, &c, 4);
}

void script_fill_video_size(script_msg_t *resp, uint32_t w, uint32_t h)
{
    memset(resp, 0, sizeof(*resp));
    resp->type = MSG_RESPONSE_VIDEO_SIZE;
    resp->data_size = 8;
    memcpy(resp->data, &w, 4);
    memcpy(resp->data + 4, &h, 4);
}

void script_fill_window_size(script_msg_t *resp, uint32_t w, uint32_t h)
{
    memset(resp, 0, sizeof(*resp));
    resp->type = MSG_RESPONSE_WINDOW_SIZE;
    resp->data_size = 8;
    memcpy(resp->data, &w, 4);
    memcpy(resp->data + 4, &h, 4);
}

void script_fill_clipboard(script_msg_t *resp, const char *text)
{
    memset(resp, 0, sizeof(*resp));
    resp->type = MSG_RESPONSE_CLIPBOARD;
    if (text) {
        size_t len = strlen(text);
        if (len > SCRIPT_MSG_MAX_DATA_SIZE - 1)
            len = SCRIPT_MSG_MAX_DATA_SIZE - 1;
        memcpy(resp->data, text, len);
        resp->data[len] = '\0';
        resp->data_size = (uint32_t)(len + 1);
    }
}

void script_fill_frame_capture(script_msg_t *resp, const uint8_t *nv12_data,
                               uint32_t width, uint32_t height, uint32_t data_size)
{
    memset(resp, 0, sizeof(*resp));
    resp->type = MSG_RESPONSE_FRAME_CAPTURE;

    if (nv12_data && data_size > 0 &&
        data_size <= SCRIPT_MSG_MAX_DATA_SIZE - 12) {
        resp->data_size = 12 + data_size;
        memcpy(resp->data, &width, 4);
        memcpy(resp->data + 4, &height, 4);
        memcpy(resp->data + 8, &data_size, 4);
        memcpy(resp->data + 12, nv12_data, data_size);
    } else {
        /* Frame too large or unavailable — return dimensions only */
        resp->data_size = 12;
        memcpy(resp->data, &width, 4);
        memcpy(resp->data + 4, &height, 4);
        uint32_t zero = 0;
        memcpy(resp->data + 8, &zero, 4);
    }
}

/* ================================================================== */
/* Init: register all FFI symbols                                      */
/* ================================================================== */

bool script_bindings_init(script_msg_queue_t *to_main, script_msg_queue_t *response_q)
{
    if (!to_main) {
        log_error("script bindings: to_main queue is NULL");
        return false;
    }
    g_to_main = to_main;
    g_response_q = response_q;

    /* Control commands */
    Sforeign_symbol("c-inject-keycode",    (void *)c_inject_keycode);
    Sforeign_symbol("c-inject-text",       (void *)c_inject_text);
    Sforeign_symbol("c-inject-touch",      (void *)c_inject_touch);
    Sforeign_symbol("c-inject-scroll",     (void *)c_inject_scroll);
    Sforeign_symbol("c-set-clipboard",     (void *)c_set_clipboard);
    Sforeign_symbol("c-expand-notification", (void *)c_expand_notification);
    Sforeign_symbol("c-collapse-panels",   (void *)c_collapse_panels);
    Sforeign_symbol("c-set-display-power", (void *)c_set_display_power);
    Sforeign_symbol("c-rotate-device",     (void *)c_rotate_device);
    Sforeign_symbol("c-start-app",         (void *)c_start_app);
    Sforeign_symbol("c-sleep-ms",          (void *)c_sleep_ms);
    Sforeign_symbol("c-log-message",       (void *)c_log_message);

    /* Synchronous queries */
    Sforeign_symbol("c-query-device-info",  (void *)c_query_device_info);
    Sforeign_symbol("c-query-video-size",   (void *)c_query_video_size);
    Sforeign_symbol("c-query-window-size",  (void *)c_query_window_size);
    Sforeign_symbol("c-query-clipboard",    (void *)c_query_clipboard);
    Sforeign_symbol("c-query-frame-capture", (void *)c_query_frame_capture);

    log_info("Script FFI bindings registered (17 symbols)");
    return true;
}
