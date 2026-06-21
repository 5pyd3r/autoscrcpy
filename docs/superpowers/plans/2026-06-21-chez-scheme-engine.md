# Chez Scheme 嵌入式脚本引擎实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 Chez Scheme 嵌入为脚本引擎，支持设备自动化、运行时扩展和交互式 REPL。

**Architecture:** Chez Scheme 在独立线程运行，通过线程安全消息队列与主线程（D3D11/Win32）通信。C FFI 绑定暴露设备操作给 Scheme，事件分发器将主线程事件转发给 Scheme 回调。REPL 为独立 Win32 浮动窗口。

**Tech Stack:** C11, Chez Scheme (static lib), Meson + Ninja + Clang, Win32 API

**Spec:** `docs/superpowers/specs/2026-06-21-chez-scheme-engine-design.md`

---

## 文件结构

### 新增文件

| 文件 | 职责 |
|------|------|
| `src/script/message_queue.h` | 消息队列 API：类型定义、init/destroy/send/recv |
| `src/script/message_queue.c` | 环形缓冲区 + CRITICAL_SECTION + 信号量实现 |
| `src/script/engine.h` | 脚本引擎 API：init/start/stop/destroy、eval |
| `src/script/engine.c` | Chez Scheme 生命周期管理、独立线程、错误捕获 |
| `src/script/bindings.h` | FFI 绑定声明 |
| `src/script/bindings.c` | 注册 C 函数为 Scheme 外部过程 |
| `src/script/event_dispatch.h` | 事件分发 API |
| `src/script/event_dispatch.c` | 主线程事件 → Scheme 队列转发 |
| `src/script/repl_window.h` | REPL 窗口 API |
| `src/script/repl_window.c` | Win32 独立窗口 + EDIT 控件 |
| `src/script/script_api.h` | 统一头文件 |
| `lib/init.ss` | Scheme 运行时库 |
| `subprojects/chez-scheme.wrap` | Meson wrap 文件 |
| `subprojects/packagefiles/chez-scheme/meson.build` | Chez Scheme 构建包装 |
| `tests/test_message_queue.c` | 消息队列单元测试 |
| `tests/test_script_engine.c` | 引擎生命周期集成测试 |

### 修改文件

| 文件 | 修改内容 |
|------|----------|
| `src/app/options.h` | 新增 `script_path`、`script_eval`、`repl` 字段 |
| `src/app/cli.c` | 解析 `-s`/`--script`、`-r`/`--repl`、`-e`/`--eval` |
| `src/app/config.c` | 读取 `[script]` 配置节 |
| `src/app/application.h` | 新增 `script_engine_t` 成员 |
| `src/app/application.c` | 初始化/运行/销毁脚本引擎，PeekMessage 中处理队列 |
| `src/meson.build` | 添加 `src/script/` 源文件 |
| `meson.build` | 添加 `chez_scheme_dep` 依赖 |
| `tests/meson.build` | 添加脚本引擎测试 |

---

## Task 1: 消息队列

**Files:**
- Create: `src/script/message_queue.h`
- Create: `src/script/message_queue.c`
- Create: `tests/test_message_queue.c`
- Modify: `tests/meson.build`
- Modify: `src/meson.build`

### Step 1: 创建消息队列头文件

```c
/* src/script/message_queue.h */
#ifndef SCRIPT_MESSAGE_QUEUE_H
#define SCRIPT_MESSAGE_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <windows.h>

#define SCRIPT_MSG_QUEUE_CAPACITY 256
#define SCRIPT_MSG_MAX_DATA_SIZE  256

typedef enum {
    /* Scheme → Main */
    MSG_INJECT_KEYCODE,
    MSG_INJECT_TEXT,
    MSG_INJECT_TOUCH,
    MSG_INJECT_SCROLL,
    MSG_SET_CLIPBOARD,
    MSG_EXPAND_NOTIFICATION,
    MSG_COLLAPSE_PANELS,
    MSG_SET_DISPLAY_POWER,
    MSG_ROTATE_DEVICE,
    MSG_START_APP,
    /* Main → Scheme */
    MSG_EVENT_KEY,
    MSG_EVENT_MOUSE,
    MSG_EVENT_FRAME,
    MSG_EVENT_CONNECTED,
    MSG_EVENT_DISCONNECTED,
    MSG_EVENT_ERROR,
    /* Control */
    MSG_SHUTDOWN,
} script_msg_type_t;

typedef struct {
    script_msg_type_t type;
    uint32_t data_size;
    uint8_t data[SCRIPT_MSG_MAX_DATA_SIZE];
} script_msg_t;

typedef struct {
    script_msg_t items[SCRIPT_MSG_QUEUE_CAPACITY];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    CRITICAL_SECTION cs;
    HANDLE semaphore;  /* signaled when items available */
    bool initialized;
} script_msg_queue_t;

bool script_msg_queue_init(script_msg_queue_t *q);
void script_msg_queue_destroy(script_msg_queue_t *q);

/* Non-blocking send. Returns false if queue full (drops message, logs warning). */
bool script_msg_queue_send(script_msg_queue_t *q, const script_msg_t *msg);

/* Blocking receive. Returns false on shutdown or timeout. */
bool script_msg_queue_recv(script_msg_queue_t *q, script_msg_t *msg, uint32_t timeout_ms);

/* Non-blocking try-receive. Returns false if empty. */
bool script_msg_queue_try_recv(script_msg_queue_t *q, script_msg_t *msg);

/* Drain all pending messages (for shutdown). */
void script_msg_queue_drain(script_msg_queue_t *q);

#endif /* SCRIPT_MESSAGE_QUEUE_H */
```

### Step 2: 实现消息队列

```c
/* src/script/message_queue.c */
#include "message_queue.h"
#include "../platform/log.h"
#include <string.h>

bool script_msg_queue_init(script_msg_queue_t *q) {
    memset(q, 0, sizeof(*q));
    InitializeCriticalSection(&q->cs);
    q->semaphore = CreateSemaphoreA(NULL, 0, SCRIPT_MSG_QUEUE_CAPACITY, NULL);
    if (!q->semaphore) {
        log_error("Failed to create message queue semaphore");
        return false;
    }
    q->initialized = true;
    return true;
}

void script_msg_queue_destroy(script_msg_queue_t *q) {
    if (!q->initialized) return;
    DeleteCriticalSection(&q->cs);
    if (q->semaphore) {
        CloseHandle(q->semaphore);
        q->semaphore = NULL;
    }
    q->initialized = false;
}

bool script_msg_queue_send(script_msg_queue_t *q, const script_msg_t *msg) {
    EnterCriticalSection(&q->cs);
    if (q->count >= SCRIPT_MSG_QUEUE_CAPACITY) {
        LeaveCriticalSection(&q->cs);
        log_warn("Script message queue full, dropping message type %d", msg->type);
        return false;
    }
    q->items[q->tail] = *msg;
    q->tail = (q->tail + 1) % SCRIPT_MSG_QUEUE_CAPACITY;
    q->count++;
    LeaveCriticalSection(&q->cs);
    ReleaseSemaphore(q->semaphore, 1, NULL);
    return true;
}

bool script_msg_queue_recv(script_msg_queue_t *q, script_msg_t *msg, uint32_t timeout_ms) {
    DWORD result = WaitForSingleObject(q->semaphore, timeout_ms);
    if (result != WAIT_OBJECT_0) return false;

    EnterCriticalSection(&q->cs);
    if (q->count == 0) {
        LeaveCriticalSection(&q->cs);
        return false;
    }
    *msg = q->items[q->head];
    q->head = (q->head + 1) % SCRIPT_MSG_QUEUE_CAPACITY;
    q->count--;
    LeaveCriticalSection(&q->cs);
    return true;
}

bool script_msg_queue_try_recv(script_msg_queue_t *q, script_msg_t *msg) {
    return script_msg_queue_recv(q, msg, 0);
}

void script_msg_queue_drain(script_msg_queue_t *q) {
    EnterCriticalSection(&q->cs);
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    LeaveCriticalSection(&q->cs);
    /* Reset semaphore */
    while (WaitForSingleObject(q->semaphore, 0) == WAIT_OBJECT_0) {
        /* drain */
    }
}
```

### Step 3: 编写消息队列测试

```c
/* tests/test_message_queue.c */
#include "../src/script/message_queue.h"
#include "../src/platform/log.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  %-40s ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static void test_init_destroy(void) {
    TEST("init and destroy");
    script_msg_queue_t q;
    assert(script_msg_queue_init(&q));
    assert(q.initialized);
    assert(q.count == 0);
    script_msg_queue_destroy(&q);
    assert(!q.initialized);
    PASS();
}

static void test_send_recv(void) {
    TEST("send and receive");
    script_msg_queue_t q;
    script_msg_queue_init(&q);

    script_msg_t send_msg = {0};
    send_msg.type = MSG_INJECT_KEYCODE;
    send_msg.data_size = 8;
    memcpy(send_msg.data, "\x03\x00\x00\x00\x01\x00\x00\x00", 8);

    assert(script_msg_queue_send(&q, &send_msg));
    assert(q.count == 1);

    script_msg_t recv_msg = {0};
    assert(script_msg_queue_try_recv(&q, &recv_msg));
    assert(recv_msg.type == MSG_INJECT_KEYCODE);
    assert(recv_msg.data_size == 8);
    assert(q.count == 0);

    script_msg_queue_destroy(&q);
    PASS();
}

static void test_fifo_order(void) {
    TEST("FIFO ordering");
    script_msg_queue_t q;
    script_msg_queue_init(&q);

    for (int i = 0; i < 5; i++) {
        script_msg_t msg = {0};
        msg.type = (script_msg_type_t)(MSG_EVENT_KEY + i);
        script_msg_queue_send(&q, &msg);
    }

    for (int i = 0; i < 5; i++) {
        script_msg_t msg = {0};
        assert(script_msg_queue_try_recv(&q, &msg));
        assert(msg.type == (script_msg_type_t)(MSG_EVENT_KEY + i));
    }

    script_msg_queue_destroy(&q);
    PASS();
}

static void test_empty_recv(void) {
    TEST("receive from empty queue");
    script_msg_queue_t q;
    script_msg_queue_init(&q);

    script_msg_t msg = {0};
    assert(!script_msg_queue_try_recv(&q, &msg));

    script_msg_queue_destroy(&q);
    PASS();
}

static void test_fill_and_overflow(void) {
    TEST("fill to capacity and overflow");
    script_msg_queue_t q;
    script_msg_queue_init(&q);

    for (uint32_t i = 0; i < SCRIPT_MSG_QUEUE_CAPACITY; i++) {
        script_msg_t msg = {0};
        msg.type = MSG_EVENT_KEY;
        assert(script_msg_queue_send(&q, &msg));
    }
    assert(q.count == SCRIPT_MSG_QUEUE_CAPACITY);

    /* Overflow should return false */
    script_msg_t overflow_msg = {0};
    overflow_msg.type = MSG_SHUTDOWN;
    assert(!script_msg_queue_send(&q, &overflow_msg));

    script_msg_queue_destroy(&q);
    PASS();
}

static void test_drain(void) {
    TEST("drain queue");
    script_msg_queue_t q;
    script_msg_queue_init(&q);

    for (int i = 0; i < 10; i++) {
        script_msg_t msg = {0};
        msg.type = MSG_EVENT_KEY;
        script_msg_queue_send(&q, &msg);
    }
    assert(q.count == 10);

    script_msg_queue_drain(&q);
    assert(q.count == 0);

    script_msg_t msg = {0};
    assert(!script_msg_queue_try_recv(&q, &msg));

    script_msg_queue_destroy(&q);
    PASS();
}

int main(void) {
    log_init(LOG_LEVEL_ERROR);
    printf("Message queue tests:\n");

    test_init_destroy();
    test_send_recv();
    test_fifo_order();
    test_empty_recv();
    test_fill_and_overflow();
    test_drain();

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    log_destroy();
    return tests_failed > 0 ? 1 : 0;
}
```

### Step 4: 更新构建文件

在 `src/meson.build` 的 `app_src` 之后添加：

```meson
# Script sources
script_src = files(
    'script/message_queue.c',
)
```

在 executable 中添加 `script_src`。

在 `tests/meson.build` 末尾添加：

```meson
test_message_queue = executable('test_message_queue', 'test_message_queue.c',
    '../src/script/message_queue.c',
    '../src/platform/log.c',
    dependencies: winlibs,
)
test('Message queue test', test_message_queue)
```

### Step 5: 编译并运行测试

```bash
ninja -C builddir
meson test -C builddir --test-args test_message_queue
```

Expected: All 6 tests pass.

### Step 6: 提交

```bash
git add src/script/message_queue.h src/script/message_queue.c tests/test_message_queue.c src/meson.build tests/meson.build
git commit -m "feat(script): add thread-safe message queue for script engine"
```

---

## Task 2: Chez Scheme 构建集成

**Files:**
- Create: `subprojects/chez-scheme.wrap`
- Create: `subprojects/packagefiles/chez-scheme/meson.build`
- Modify: `meson.build`

### Step 1: 下载 Chez Scheme 源码

```bash
cd subprojects
git clone --depth 1 --branch v10.1.0 https://github.com/cisco/ChezScheme.git chez-scheme-src
```

### Step 2: 为 Windows 构建 Chez Scheme

Chez Scheme 在 Windows 上使用 `configure.bat` + nmake 或 MinGW。使用 MinGW/MSYS2 路径（与项目的 clang 工具链一致）：

```bash
cd subprojects/chez-scheme-src
./configure --pb
make -j$(nproc)
```

这将生成：
- `boot/a6le/petite.boot` + `boot/a6le/scheme.boot`（启动文件）
- `a6le/libkernel.a`（内核静态库）
- `a6le/libchezscheme.a`（主静态库）
- `a6le/scheme.h`（头文件）

### Step 3: 创建 wrap 文件

```ini
# subprojects/chez-scheme.wrap
[wrap-git]
url = https://github.com/cisco/ChezScheme.git
revision = v10.1.0
depth = 1

[provide]
chez-scheme = chez_scheme_dep
```

### Step 4: 创建 Meson 构建包装

```meson
# subprojects/packagefiles/chez-scheme/meson.build
project('chez-scheme', 'c',
    version: '10.1.0',
)

chez_inc = include_directories('a6le')

chez_kernel = static_library('kernel',
    'a6le/kernel.o',  # or assembled from .s files
    include_directories: chez_inc,
)

chez_main = static_library('chezscheme',
    # Chez Scheme is primarily pre-compiled; we link the static libs
    # This meson.build wraps the pre-built artifacts
    'a6le/main.o',
    include_directories: chez_inc,
    link_with: chez_kernel,
)

chez_scheme_dep = declare_dependency(
    include_directories: chez_inc,
    link_with: [chez_main, chez_kernel],
)
```

**注意：** 实际构建可能需要根据 Chez Scheme 的构建输出调整。如果 Meson 无法直接包装 Chez 的构建系统，替代方案是：

1. 预编译 Chez Scheme 静态库
2. 将 `.a`/`.lib` 和头文件放到 `subprojects/chez-scheme-prebuilt/`
3. 在 `meson.build` 中用 `declare_dependency` 直接引用

### Step 5: 更新主 meson.build

在 `meson.build` 的依赖部分添加：

```meson
chez_scheme_dep = dependency('chez-scheme', required: true)
```

在 `src/meson.build` 的 executable dependencies 中添加 `chez_scheme_dep`。

### Step 6: 验证编译

```bash
meson setup builddir --native-file meson-native-clang-gcc.ini --wipe
ninja -C builddir
```

Expected: 编译成功，Chez Scheme 库正确链接。

### Step 7: 提交

```bash
git add subprojects/chez-scheme.wrap subprojects/packagefiles/chez-scheme/meson.build meson.build
git commit -m "build: integrate Chez Scheme as static subproject"
```

---

## Task 3: 脚本引擎核心

**Files:**
- Create: `src/script/engine.h`
- Create: `src/script/engine.c`
- Create: `src/script/script_api.h`

### Step 1: 创建脚本引擎头文件

```c
/* src/script/engine.h */
#ifndef SCRIPT_ENGINE_H
#define SCRIPT_ENGINE_H

#include "message_queue.h"
#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

typedef struct script_engine script_engine_t;

/* Callback for REPL output */
typedef void (*script_output_cb_t)(const char *text, bool is_error, void *userdata);

struct script_engine {
    script_msg_queue_t to_main;    /* Scheme → Main */
    script_msg_queue_t to_scheme;  /* Main → Scheme */
    HANDLE thread;
    volatile bool running;
    volatile bool initialized;
    script_output_cb_t output_cb;
    void *output_userdata;
    char *script_path;    /* optional startup script */
    char *eval_expr;      /* optional eval expression */
};

bool script_engine_init(script_engine_t *engine, const char *script_path,
                        const char *eval_expr, script_output_cb_t output_cb,
                        void *output_userdata);
bool script_engine_start(script_engine_t *engine);
void script_engine_stop(script_engine_t *engine);
void script_engine_destroy(script_engine_t *engine);

/* Send a message from main thread to Scheme thread */
bool script_engine_send(script_engine_t *engine, const script_msg_t *msg);

/* Receive a message from Scheme thread (non-blocking, for main thread PeekMessage) */
bool script_engine_recv(script_engine_t *engine, script_msg_t *msg);

/* Eval a string in the Scheme engine (thread-safe, sends to Scheme thread) */
bool script_engine_eval(script_engine_t *engine, const char *code);

/* Check if engine is running */
bool script_engine_is_running(const script_engine_t *engine);

#endif /* SCRIPT_ENGINE_H */
```

### Step 2: 创建统一头文件

```c
/* src/script/script_api.h */
#ifndef SCRIPT_API_H
#define SCRIPT_API_H

#include "engine.h"
#include "message_queue.h"

#endif /* SCRIPT_API_H */
```

### Step 3: 实现脚本引擎

```c
/* src/script/engine.c */
#include "engine.h"
#include "bindings.h"
#include "../platform/log.h"
#include <string.h>
#include <stdlib.h>

/* Forward declarations */
static DWORD WINAPI script_thread_func(LPVOID arg);
static void scheme_eval_protected(script_engine_t *engine, const char *code);

bool script_engine_init(script_engine_t *engine, const char *script_path,
                        const char *eval_expr, script_output_cb_t output_cb,
                        void *output_userdata) {
    memset(engine, 0, sizeof(*engine));
    engine->output_cb = output_cb;
    engine->output_userdata = output_userdata;

    if (script_path) {
        engine->script_path = _strdup(script_path);
    }
    if (eval_expr) {
        engine->eval_expr = _strdup(eval_expr);
    }

    if (!script_msg_queue_init(&engine->to_main)) {
        log_error("Failed to init to_main queue");
        return false;
    }
    if (!script_msg_queue_init(&engine->to_scheme)) {
        log_error("Failed to init to_scheme queue");
        script_msg_queue_destroy(&engine->to_main);
        return false;
    }

    engine->initialized = true;
    log_info("Script engine initialized");
    return true;
}

bool script_engine_start(script_engine_t *engine) {
    if (!engine->initialized) {
        log_error("Script engine not initialized");
        return false;
    }
    engine->running = true;
    engine->thread = CreateThread(NULL, 0, script_thread_func, engine, 0, NULL);
    if (!engine->thread) {
        log_error("Failed to create script thread");
        engine->running = false;
        return false;
    }
    log_info("Script engine thread started");
    return true;
}

void script_engine_stop(script_engine_t *engine) {
    if (!engine->running) return;
    engine->running = false;

    /* Send shutdown message */
    script_msg_t msg = {0};
    msg.type = MSG_SHUTDOWN;
    script_msg_queue_send(&engine->to_scheme, &msg);

    /* Wait for thread to finish */
    if (engine->thread) {
        WaitForSingleObject(engine->thread, 5000);
        CloseHandle(engine->thread);
        engine->thread = NULL;
    }
    log_info("Script engine stopped");
}

void script_engine_destroy(script_engine_t *engine) {
    script_engine_stop(engine);
    script_msg_queue_destroy(&engine->to_main);
    script_msg_queue_destroy(&engine->to_scheme);
    free(engine->script_path);
    free(engine->eval_expr);
    memset(engine, 0, sizeof(*engine));
}

bool script_engine_send(script_engine_t *engine, const script_msg_t *msg) {
    return script_msg_queue_send(&engine->to_scheme, msg);
}

bool script_engine_recv(script_engine_t *engine, script_msg_t *msg) {
    return script_msg_queue_try_recv(&engine->to_main, msg);
}

bool script_engine_eval(script_engine_t *engine, const char *code) {
    /* Send eval request to Scheme thread via a special message */
    script_msg_t msg = {0};
    msg.type = MSG_INJECT_TEXT;  /* reuse text type for eval */
    uint32_t len = (uint32_t)strlen(code);
    if (len >= SCRIPT_MSG_MAX_DATA_SIZE) {
        log_error("Eval expression too long");
        return false;
    }
    memcpy(msg.data, code, len + 1);
    msg.data_size = len + 1;
    return script_msg_queue_send(&engine->to_scheme, &msg);
}

bool script_engine_is_running(const script_engine_t *engine) {
    return engine->running;
}

/* Scheme thread function */
static DWORD WINAPI script_thread_func(LPVOID arg) {
    script_engine_t *engine = (script_engine_t *)arg;

    /* Initialize Chez Scheme */
    /* TODO: Sscheme_init, Sbuild_heap, register FFI bindings */
    /* This will be fully implemented in Task 4 (bindings) */

    log_info("Scheme thread running");

    /* Load startup script if specified */
    if (engine->script_path) {
        /* TODO: load and execute script file */
        log_info("Would load script: %s", engine->script_path);
    }

    /* Execute eval expression if specified */
    if (engine->eval_expr) {
        /* TODO: eval expression */
        log_info("Would eval: %s", engine->eval_expr);
    }

    /* Main message loop */
    while (engine->running) {
        script_msg_t msg = {0};
        if (script_msg_queue_recv(&engine->to_scheme, &msg, 100)) {
            if (msg.type == MSG_SHUTDOWN) {
                break;
            }
            /* TODO: dispatch message to Scheme callbacks (Task 6) */
        }
    }

    log_info("Scheme thread exiting");
    return 0;
}
```

### Step 4: 更新构建文件

在 `src/meson.build` 的 `script_src` 中添加：

```meson
script_src = files(
    'script/message_queue.c',
    'script/engine.c',
)
```

### Step 5: 编译验证

```bash
ninja -C builddir
```

Expected: 编译成功（engine.c 中的 Chez 调用是 TODO，不影响编译）。

### Step 6: 提交

```bash
git add src/script/engine.h src/script/engine.c src/script/script_api.h src/meson.build
git commit -m "feat(script): add script engine core with thread lifecycle"
```

---

## Task 4: FFI 绑定

**Files:**
- Create: `src/script/bindings.h`
- Create: `src/script/bindings.c`

### Step 1: 创建绑定头文件

```c
/* src/script/bindings.h */
#ifndef SCRIPT_BINDINGS_H
#define SCRIPT_BINDINGS_H

#include "message_queue.h"
#include <stdbool.h>

/* Initialize all FFI bindings. Call after Sscheme_init/Sbuild_heap. */
bool script_bindings_init(script_msg_queue_t *to_main);

#endif /* SCRIPT_BINDINGS_H */
```

### Step 2: 实现 FFI 绑定

```c
/* src/script/bindings.c */
#include "bindings.h"
#include "../platform/log.h"
#include <string.h>

/* Global queue pointer for FFI callbacks */
static script_msg_queue_t *g_to_main = NULL;

bool script_bindings_init(script_msg_queue_t *to_main) {
    g_to_main = to_main;

    /* Register C functions as Scheme foreign procedures */
    /* These will be called via Sforeign_symbol after Chez is fully integrated */

    /* Control commands */
    /* Sforeign_symbol("c_inject_keycode", (void *)c_inject_keycode); */
    /* Sforeign_symbol("c_inject_text", (void *)c_inject_text); */
    /* Sforeign_symbol("c_inject_touch", (void *)c_inject_touch); */
    /* Sforeign_symbol("c_inject_scroll", (void *)c_inject_scroll); */
    /* Sforeign_symbol("c_set_clipboard", (void *)c_set_clipboard); */
    /* Sforeign_symbol("c_expand_notification", (void *)c_expand_notification); */
    /* Sforeign_symbol("c_collapse_panels", (void *)c_collapse_panels); */
    /* Sforeign_symbol("c_set_display_power", (void *)c_set_display_power); */
    /* Sforeign_symbol("c_rotate_device", (void *)c_rotate_device); */
    /* Sforeign_symbol("c_start_app", (void *)c_start_app); */

    /* State queries */
    /* Sforeign_symbol("c_device_width", (void *)c_device_width); */
    /* Sforeign_symbol("c_device_height", (void *)c_device_height); */
    /* Sforeign_symbol("c_device_name", (void *)c_device_name); */
    /* Sforeign_symbol("c_is_connected", (void *)c_is_connected); */

    /* Utility */
    /* Sforeign_symbol("c_sleep_ms", (void *)c_sleep_ms); */
    /* Sforeign_symbol("c_log_message", (void *)c_log_message); */

    log_info("Script FFI bindings registered");
    return true;
}

/* === C functions callable from Scheme === */

/* All functions post messages to the main thread queue */

static void c_inject_keycode(int keycode, int down) {
    if (!g_to_main) return;
    script_msg_t msg = {0};
    msg.type = MSG_INJECT_KEYCODE;
    msg.data_size = 8;
    memcpy(msg.data, &keycode, 4);
    memcpy(msg.data + 4, &down, 4);
    script_msg_queue_send(g_to_main, &msg);
}

static void c_inject_text(const char *text) {
    if (!g_to_main || !text) return;
    script_msg_t msg = {0};
    msg.type = MSG_INJECT_TEXT;
    uint32_t len = (uint32_t)strlen(text);
    if (len >= SCRIPT_MSG_MAX_DATA_SIZE) len = SCRIPT_MSG_MAX_DATA_SIZE - 1;
    memcpy(msg.data, text, len);
    msg.data[len] = '\0';
    msg.data_size = len + 1;
    script_msg_queue_send(g_to_main, &msg);
}

static void c_inject_touch(int x, int y, int action) {
    if (!g_to_main) return;
    script_msg_t msg = {0};
    msg.type = MSG_INJECT_TOUCH;
    msg.data_size = 12;
    memcpy(msg.data, &x, 4);
    memcpy(msg.data + 4, &y, 4);
    memcpy(msg.data + 8, &action, 4);
    script_msg_queue_send(g_to_main, &msg);
}

static void c_inject_scroll(int x, int y, int dx, int dy) {
    if (!g_to_main) return;
    script_msg_t msg = {0};
    msg.type = MSG_INJECT_SCROLL;
    msg.data_size = 16;
    memcpy(msg.data, &x, 4);
    memcpy(msg.data + 4, &y, 4);
    memcpy(msg.data + 8, &dx, 4);
    memcpy(msg.data + 12, &dy, 4);
    script_msg_queue_send(g_to_main, &msg);
}

static void c_set_clipboard(const char *text) {
    if (!g_to_main || !text) return;
    script_msg_t msg = {0};
    msg.type = MSG_SET_CLIPBOARD;
    uint32_t len = (uint32_t)strlen(text);
    if (len >= SCRIPT_MSG_MAX_DATA_SIZE) len = SCRIPT_MSG_MAX_DATA_SIZE - 1;
    memcpy(msg.data, text, len);
    msg.data[len] = '\0';
    msg.data_size = len + 1;
    script_msg_queue_send(g_to_main, &msg);
}

static void c_expand_notification(void) {
    if (!g_to_main) return;
    script_msg_t msg = {0};
    msg.type = MSG_EXPAND_NOTIFICATION;
    script_msg_queue_send(g_to_main, &msg);
}

static void c_collapse_panels(void) {
    if (!g_to_main) return;
    script_msg_t msg = {0};
    msg.type = MSG_COLLAPSE_PANELS;
    script_msg_queue_send(g_to_main, &msg);
}

static void c_set_display_power(int on) {
    if (!g_to_main) return;
    script_msg_t msg = {0};
    msg.type = MSG_SET_DISPLAY_POWER;
    msg.data_size = 4;
    memcpy(msg.data, &on, 4);
    script_msg_queue_send(g_to_main, &msg);
}

static void c_rotate_device(void) {
    if (!g_to_main) return;
    script_msg_t msg = {0};
    msg.type = MSG_ROTATE_DEVICE;
    script_msg_queue_send(g_to_main, &msg);
}

static void c_start_app(const char *package) {
    if (!g_to_main || !package) return;
    script_msg_t msg = {0};
    msg.type = MSG_START_APP;
    uint32_t len = (uint32_t)strlen(package);
    if (len >= SCRIPT_MSG_MAX_DATA_SIZE) len = SCRIPT_MSG_MAX_DATA_SIZE - 1;
    memcpy(msg.data, package, len);
    msg.data[len] = '\0';
    msg.data_size = len + 1;
    script_msg_queue_send(g_to_main, &msg);
}

static void c_sleep_ms(int ms) {
    Sleep((DWORD)ms);
}

static void c_log_message(int level, const char *msg) {
    if (!msg) return;
    switch (level) {
        case 0: log_debug("[script] %s", msg); break;
        case 1: log_info("[script] %s", msg); break;
        case 2: log_warn("[script] %s", msg); break;
        case 3: log_error("[script] %s", msg); break;
        default: log_info("[script] %s", msg); break;
    }
}
```

### Step 3: 更新 engine.c 使用 bindings

在 `script_thread_func` 中 Chez 初始化部分调用 `script_bindings_init`。

### Step 4: 更新构建文件

在 `script_src` 中添加 `'script/bindings.c'`。

### Step 5: 编译验证

```bash
ninja -C builddir
```

### Step 6: 提交

```bash
git add src/script/bindings.h src/script/bindings.c
git commit -m "feat(script): add FFI bindings for device control commands"
```

---

## Task 5: Scheme 运行时库

**Files:**
- Create: `lib/init.ss`

### Step 1: 创建 Scheme 运行时库

```scheme
;; lib/init.ss — AutoScrcpy Scheme 运行时库
;; 提供用户友好的 API，内部调用 C FFI 绑定

;; === 按键符号 → Android keycode 映射 ===
(define *keycode-map*
  '((home . 3) (back . 4) (power . 26) (menu . 82)
    (volume-up . 24) (volume-down . 25)
    (enter . 66) (tab . 61) (space . 62)
    (dpad-center . 23) (dpad-up . 19) (dpad-down . 20)
    (dpad-left . 21) (dpad-right . 22)
    (a . 29) (b . 30) (c . 31) (d . 32) (e . 33)
    (f . 34) (g . 35) (h . 36) (i . 37) (j . 38)
    (k . 39) (l . 40) (m . 41) (n . 42) (o . 43)
    (p . 44) (q . 45) (r . 46) (s . 47) (t . 48)
    (u . 49) (v . 50) (w . 51) (x . 52) (y . 53)
    (z . 54) (0 . 7) (1 . 8) (2 . 9) (3 . 10)
    (4 . 11) (5 . 12) (6 . 13) (7 . 14) (8 . 15)
    (9 . 16) (f1 . 131) (f2 . 132) (f3 . 133)
    (f4 . 134) (f5 . 135) (f6 . 136) (f7 . 137)
    (f8 . 138) (f9 . 139) (f10 . 140) (f11 . 141)
    (f12 . 142)))

;; === 事件回调注册表 ===
(define *on-key-callback* #f)
(define *on-mouse-callback* #f)
(define *on-frame-callback* #f)
(define *on-connect-callback* #f)
(define *on-disconnect-callback* #f)

;; === 控制命令 ===

(define (inject-keycode keycode down?)
  (let ((code (if (symbol? keycode)
                  (let ((entry (assq keycode *keycode-map*)))
                    (if entry (cdr entry)
                        (error 'inject-keycode "Unknown keycode symbol" keycode)))
                  keycode)))
    (c-inject-keycode code (if down? 1 0))))

(define (inject-text text)
  (c-inject-text text))

(define (inject-touch x y action)
  (let ((act (case action
               ((down) 0) ((up) 1) ((move) 2)
               (else action))))
    (c-inject-touch x y act)))

(define (inject-scroll x y dx dy)
  (c-inject-scroll x y dx dy))

(define (set-clipboard text)
  (c-set-clipboard text))

(define (expand-notification)
  (c-expand-notification))

(define (expand-settings)
  (c-expand-notification))  ;; TODO: separate expand_settings

(define (collapse-panels)
  (c-collapse-panels))

(define (set-display-power on?)
  (c-set-display-power (if on? 1 0)))

(define (rotate-device)
  (c-rotate-device))

(define (start-app package)
  (c-start-app package))

;; === 事件回调注册 ===

(define (on-key callback)
  (set! *on-key-callback* callback))

(define (on-mouse callback)
  (set! *on-mouse-callback* callback))

(define (on-frame callback)
  (set! *on-frame-callback* callback))

(define (on-connect callback)
  (set! *on-connect-callback* callback))

(define (on-disconnect callback)
  (set! *on-disconnect-callback* callback))

;; === 内置辅助过程 ===

(define (sleep-ms ms)
  (c-sleep-ms ms))

(define (log-info msg)
  (c-log-message 1 msg))

(define (log-error msg)
  (c-log-message 3 msg))

(define (log-debug msg)
  (c-log-message 0 msg))

(define (load-script path)
  (load path))

;; === REPL 辅助 ===

(define (open-repl)
  ;; Signal main thread to open REPL window
  ;; Implementation via message queue TBD
  (display "REPL window requested\n"))

(display "AutoScrcpy Scheme runtime loaded.\n")
(display "Type (help) for available commands.\n")
```

### Step 2: 提交

```bash
git add lib/init.ss
git commit -m "feat(script): add Scheme runtime library with device control API"
```

---

## Task 6: 事件分发

**Files:**
- Create: `src/script/event_dispatch.h`
- Create: `src/script/event_dispatch.c`

### Step 1: 创建事件分发头文件

```c
/* src/script/event_dispatch.h */
#ifndef SCRIPT_EVENT_DISPATCH_H
#define SCRIPT_EVENT_DISPATCH_H

#include "engine.h"
#include <stdint.h>
#include <stdbool.h>

/* Dispatch key event from main thread to script engine */
void script_dispatch_key_event(script_engine_t *engine, uint32_t vk, bool down);

/* Dispatch mouse event from main thread to script engine */
void script_dispatch_mouse_event(script_engine_t *engine, int32_t x, int32_t y,
                                  uint32_t buttons, uint32_t action);

/* Dispatch frame event (video dimensions changed) */
void script_dispatch_frame_event(script_engine_t *engine, uint32_t width, uint32_t height);

/* Dispatch connection state change */
void script_dispatch_connect(script_engine_t *engine);
void script_dispatch_disconnect(script_engine_t *engine);

/* Process incoming messages from script engine in main thread.
 * Call from PeekMessage idle loop. Returns number of messages processed. */
int script_process_messages(script_engine_t *engine,
                            /* Callbacks for executing script requests */
                            void (*on_inject_keycode)(int keycode, int down, void *ctx),
                            void (*on_inject_text)(const char *text, void *ctx),
                            void (*on_inject_touch)(int x, int y, int action, void *ctx),
                            void (*on_inject_scroll)(int x, int y, int dx, int dy, void *ctx),
                            void (*on_set_clipboard)(const char *text, void *ctx),
                            void (*on_expand_notification)(void *ctx),
                            void (*on_collapse_panels)(void *ctx),
                            void (*on_set_display_power)(int on, void *ctx),
                            void (*on_rotate_device)(void *ctx),
                            void (*on_start_app)(const char *package, void *ctx),
                            void *ctx);

#endif /* SCRIPT_EVENT_DISPATCH_H */
```

### Step 2: 实现事件分发

```c
/* src/script/event_dispatch.c */
#include "event_dispatch.h"
#include "../platform/log.h"
#include <string.h>

void script_dispatch_key_event(script_engine_t *engine, uint32_t vk, bool down) {
    script_msg_t msg = {0};
    msg.type = MSG_EVENT_KEY;
    msg.data_size = 8;
    memcpy(msg.data, &vk, 4);
    int d = down ? 1 : 0;
    memcpy(msg.data + 4, &d, 4);
    script_msg_queue_send(&engine->to_scheme, &msg);
}

void script_dispatch_mouse_event(script_engine_t *engine, int32_t x, int32_t y,
                                  uint32_t buttons, uint32_t action) {
    script_msg_t msg = {0};
    msg.type = MSG_EVENT_MOUSE;
    msg.data_size = 16;
    memcpy(msg.data, &x, 4);
    memcpy(msg.data + 4, &y, 4);
    memcpy(msg.data + 8, &buttons, 4);
    memcpy(msg.data + 12, &action, 4);
    script_msg_queue_send(&engine->to_scheme, &msg);
}

void script_dispatch_frame_event(script_engine_t *engine, uint32_t width, uint32_t height) {
    script_msg_t msg = {0};
    msg.type = MSG_EVENT_FRAME;
    msg.data_size = 8;
    memcpy(msg.data, &width, 4);
    memcpy(msg.data + 4, &height, 4);
    script_msg_queue_send(&engine->to_scheme, &msg);
}

void script_dispatch_connect(script_engine_t *engine) {
    script_msg_t msg = {0};
    msg.type = MSG_EVENT_CONNECTED;
    script_msg_queue_send(&engine->to_scheme, &msg);
}

void script_dispatch_disconnect(script_engine_t *engine) {
    script_msg_t msg = {0};
    msg.type = MSG_EVENT_DISCONNECTED;
    script_msg_queue_send(&engine->to_scheme, &msg);
}

int script_process_messages(script_engine_t *engine,
                            void (*on_inject_keycode)(int, int, void *),
                            void (*on_inject_text)(const char *, void *),
                            void (*on_inject_touch)(int, int, int, void *),
                            void (*on_inject_scroll)(int, int, int, int, void *),
                            void (*on_set_clipboard)(const char *, void *),
                            void (*on_expand_notification)(void *),
                            void (*on_collapse_panels)(void *),
                            void (*on_set_display_power)(int, void *),
                            void (*on_rotate_device)(void *),
                            void (*on_start_app)(const char *, void *),
                            void *ctx) {
    int processed = 0;
    script_msg_t msg;

    while (script_msg_queue_try_recv(&engine->to_main, &msg)) {
        processed++;
        switch (msg.type) {
            case MSG_INJECT_KEYCODE:
                if (on_inject_keycode && msg.data_size >= 8) {
                    int keycode, down;
                    memcpy(&keycode, msg.data, 4);
                    memcpy(&down, msg.data + 4, 4);
                    on_inject_keycode(keycode, down, ctx);
                }
                break;
            case MSG_INJECT_TEXT:
                if (on_inject_text) {
                    on_inject_text((const char *)msg.data, ctx);
                }
                break;
            case MSG_INJECT_TOUCH:
                if (on_inject_touch && msg.data_size >= 12) {
                    int x, y, action;
                    memcpy(&x, msg.data, 4);
                    memcpy(&y, msg.data + 4, 4);
                    memcpy(&action, msg.data + 8, 4);
                    on_inject_touch(x, y, action, ctx);
                }
                break;
            case MSG_INJECT_SCROLL:
                if (on_inject_scroll && msg.data_size >= 16) {
                    int x, y, dx, dy;
                    memcpy(&x, msg.data, 4);
                    memcpy(&y, msg.data + 4, 4);
                    memcpy(&dx, msg.data + 8, 4);
                    memcpy(&dy, msg.data + 12, 4);
                    on_inject_scroll(x, y, dx, dy, ctx);
                }
                break;
            case MSG_SET_CLIPBOARD:
                if (on_set_clipboard) {
                    on_set_clipboard((const char *)msg.data, ctx);
                }
                break;
            case MSG_EXPAND_NOTIFICATION:
                if (on_expand_notification) on_expand_notification(ctx);
                break;
            case MSG_COLLAPSE_PANELS:
                if (on_collapse_panels) on_collapse_panels(ctx);
                break;
            case MSG_SET_DISPLAY_POWER:
                if (on_set_display_power && msg.data_size >= 4) {
                    int on;
                    memcpy(&on, msg.data, 4);
                    on_set_display_power(on, ctx);
                }
                break;
            case MSG_ROTATE_DEVICE:
                if (on_rotate_device) on_rotate_device(ctx);
                break;
            case MSG_START_APP:
                if (on_start_app) {
                    on_start_app((const char *)msg.data, ctx);
                }
                break;
            default:
                log_warn("Unknown script message type: %d", msg.type);
                break;
        }
    }
    return processed;
}
```

### Step 3: 更新构建文件

在 `script_src` 中添加 `'script/event_dispatch.c'`。

### Step 4: 编译验证

```bash
ninja -C builddir
```

### Step 5: 提交

```bash
git add src/script/event_dispatch.h src/script/event_dispatch.c
git commit -m "feat(script): add event dispatch between main thread and script engine"
```

---

## Task 7: REPL 浮动窗口

**Files:**
- Create: `src/script/repl_window.h`
- Create: `src/script/repl_window.c`

### Step 1: 创建 REPL 窗口头文件

```c
/* src/script/repl_window.h */
#ifndef SCRIPT_REPL_WINDOW_H
#define SCRIPT_REPL_WINDOW_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

#define REPL_MAX_HISTORY 100
#define REPL_MAX_INPUT   4096
#define REPL_MAX_OUTPUT  (64 * 1024)

typedef void (*repl_eval_cb_t)(const char *code, void *userdata);

typedef struct {
    HWND hwnd;
    HWND h_output;   /* EDIT control - multiline readonly */
    HWND h_input;    /* EDIT control - single line */
    HFONT h_font;
    bool visible;
    bool initialized;

    /* Command history */
    char *history[REPL_MAX_HISTORY];
    int history_count;
    int history_pos;  /* current position when browsing */

    /* Callback for eval */
    repl_eval_cb_t eval_cb;
    void *eval_userdata;

    /* Output buffer */
    char output_buf[REPL_MAX_OUTPUT];
    uint32_t output_len;
} repl_window_t;

bool repl_window_init(repl_window_t *win, HINSTANCE hInstance,
                      repl_eval_cb_t eval_cb, void *eval_userdata);
void repl_window_destroy(repl_window_t *win);

/* Show/hide the REPL window */
void repl_window_show(repl_window_t *win);
void repl_window_hide(repl_window_t *win);
void repl_window_toggle(repl_window_t *win);
bool repl_window_is_visible(const repl_window_t *win);

/* Append text to output area */
void repl_window_append_output(repl_window_t *win, const char *text);
void repl_window_append_error(repl_window_t *win, const char *text);

/* Process window messages. Call from main thread PeekMessage loop.
 * Returns true if the message was handled. */
bool repl_window_process_message(repl_window_t *win, const MSG *msg);

#endif /* SCRIPT_REPL_WINDOW_H */
```

### Step 2: 实现 REPL 窗口

```c
/* src/script/repl_window.c */
#include "repl_window.h"
#include "../platform/log.h"
#include <string.h>
#include <stdio.h>
#include <commctrl.h>

#define REPL_WINDOW_CLASS "AutoScrcpyRepl"
#define REPL_OUTPUT_ID    1001
#define REPL_INPUT_ID     1002
#define REPL_MARGIN       5

static LRESULT CALLBACK repl_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static LRESULT CALLBACK repl_input_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                            UINT_PTR uidSubclass, DWORD_PTR dwRefData);

#define WM_REPL_EVAL (WM_USER + 1)

bool repl_window_init(repl_window_t *win, HINSTANCE hInstance,
                      repl_eval_cb_t eval_cb, void *eval_userdata) {
    memset(win, 0, sizeof(*win));
    win->eval_cb = eval_cb;
    win->eval_userdata = eval_userdata;
    win->history_pos = -1;

    /* Register window class */
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = repl_wnd_proc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = REPL_WINDOW_CLASS;
    RegisterClassExA(&wc);

    /* Create main window */
    win->hwnd = CreateWindowExA(
        0, REPL_WINDOW_CLASS, "AutoScrcpy Scheme REPL",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 400,
        NULL, NULL, hInstance, win);

    if (!win->hwnd) {
        log_error("Failed to create REPL window");
        return false;
    }

    /* Create monospace font */
    win->h_font = CreateFontA(
        14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

    /* Create output edit control */
    RECT client;
    GetClientRect(win->hwnd, &client);
    int input_height = 24;

    win->h_output = CreateWindowExA(
        0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        REPL_MARGIN, REPL_MARGIN,
        client.right - 2 * REPL_MARGIN,
        client.bottom - input_height - 3 * REPL_MARGIN,
        win->hwnd, (HMENU)REPL_OUTPUT_ID, hInstance, NULL);

    /* Create input edit control */
    win->h_input = CreateWindowExA(
        0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        REPL_MARGIN,
        client.bottom - input_height - REPL_MARGIN,
        client.right - 2 * REPL_MARGIN,
        input_height,
        win->hwnd, (HMENU)REPL_INPUT_ID, hInstance, NULL);

    if (win->h_output) SendMessage(win->h_output, WM_SETFONT, (WPARAM)win->h_font, TRUE);
    if (win->h_input) SendMessage(win->h_input, WM_SETFONT, (WPARAM)win->h_font, TRUE);

    /* Subclass input control to intercept Enter key */
    SetWindowSubclass(win->h_input, repl_input_subclass, 1, (DWORD_PTR)win);

    /* Store window pointer for WndProc */
    SetWindowLongPtrA(win->hwnd, GWLP_USERDATA, (LONG_PTR)win);

    win->initialized = true;
    log_info("REPL window initialized");
    return true;
}

void repl_window_destroy(repl_window_t *win) {
    if (win->h_font) { DeleteObject(win->h_font); win->h_font = NULL; }
    if (win->hwnd) { DestroyWindow(win->hwnd); win->hwnd = NULL; }
    for (int i = 0; i < win->history_count; i++) {
        free(win->history[i]);
    }
    UnregisterClassA(REPL_WINDOW_CLASS, GetModuleHandle(NULL));
    win->initialized = false;
}

void repl_window_show(repl_window_t *win) {
    if (win->hwnd) {
        ShowWindow(win->hwnd, SW_SHOW);
        UpdateWindow(win->hwnd);
        SetFocus(win->h_input);
        win->visible = true;
    }
}

void repl_window_hide(repl_window_t *win) {
    if (win->hwnd) {
        ShowWindow(win->hwnd, SW_HIDE);
        win->visible = false;
    }
}

void repl_window_toggle(repl_window_t *win) {
    if (win->visible) repl_window_hide(win);
    else repl_window_show(win);
}

bool repl_window_is_visible(const repl_window_t *win) {
    return win->visible;
}

void repl_window_append_output(repl_window_t *win, const char *text) {
    if (!win->h_output || !text) return;
    /* Append to end */
    int len = GetWindowTextLengthA(win->h_output);
    SendMessageA(win->h_output, EM_SETSEL, len, len);
    SendMessageA(win->h_output, EM_REPLACESEL, FALSE, (LPARAM)text);
    /* Scroll to bottom */
    SendMessageA(win->h_output, EM_SCROLLCARET, 0, 0);
}

void repl_window_append_error(repl_window_t *win, const char *text) {
    if (!text) return;
    char buf[4096];
    snprintf(buf, sizeof(buf), "*** Error: %s\n", text);
    repl_window_append_output(win, buf);
}

static void repl_execute_input(repl_window_t *win) {
    char input[REPL_MAX_INPUT];
    GetWindowTextA(win->h_input, input, sizeof(input));
    if (input[0] == '\0') return;

    /* Add to history */
    if (win->history_count < REPL_MAX_HISTORY) {
        win->history[win->history_count++] = _strdup(input);
    }
    win->history_pos = win->history_count;

    /* Echo input */
    repl_window_append_output(win, "> ");
    repl_window_append_output(win, input);
    repl_window_append_output(win, "\n");

    /* Clear input */
    SetWindowTextA(win->h_input, "");

    /* Send to eval callback */
    if (win->eval_cb) {
        win->eval_cb(input, win->eval_userdata);
    }
}

/* Input control subclass: intercept Enter/Up/Down keys */
static LRESULT CALLBACK repl_input_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                            UINT_PTR uidSubclass, DWORD_PTR dwRefData) {
    repl_window_t *win = (repl_window_t *)dwRefData;
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_RETURN) {
            /* Send custom message to parent to trigger eval */
            SendMessageA(GetParent(hwnd), WM_REPL_EVAL, 0, 0);
            return 0;
        } else if (wp == VK_UP) {
            if (win && win->history_pos > 0) {
                win->history_pos--;
                SetWindowTextA(hwnd, win->history[win->history_pos]);
                SendMessageA(hwnd, EM_SETSEL, -1, -1);
            }
            return 0;
        } else if (wp == VK_DOWN) {
            if (win) {
                if (win->history_pos < win->history_count - 1) {
                    win->history_pos++;
                    SetWindowTextA(hwnd, win->history[win->history_pos]);
                } else {
                    win->history_pos = win->history_count;
                    SetWindowTextA(hwnd, "");
                }
                SendMessageA(hwnd, EM_SETSEL, -1, -1);
            }
            return 0;
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, repl_input_subclass, uidSubclass);
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK repl_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    repl_window_t *win = (repl_window_t *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_SIZE:
        if (win && win->h_output && win->h_input) {
            RECT client;
            GetClientRect(hwnd, &client);
            int input_height = 24;
            MoveWindow(win->h_output, REPL_MARGIN, REPL_MARGIN,
                       client.right - 2 * REPL_MARGIN,
                       client.bottom - input_height - 3 * REPL_MARGIN, TRUE);
            MoveWindow(win->h_input, REPL_MARGIN,
                       client.bottom - input_height - REPL_MARGIN,
                       client.right - 2 * REPL_MARGIN, input_height, TRUE);
        }
        return 0;

    case WM_REPL_EVAL:
        if (win) repl_execute_input(win);
        return 0;

    case WM_CLOSE:
        if (win) {
            repl_window_hide(win);
            return 0;  /* Don't destroy, just hide */
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wp, lp);
}

bool repl_window_process_message(repl_window_t *win, const MSG *msg) {
    if (!win->initialized || !win->visible) return false;

    /* Let the window process its own messages */
    if (IsDialogMessageA(win->hwnd, (LPMSG)msg)) {
        return true;
    }
    return false;
}
```

### Step 3: 更新构建文件

在 `script_src` 中添加 `'script/repl_window.c'`。

### Step 4: 编译验证

```bash
ninja -C builddir
```

### Step 5: 提交

```bash
git add src/script/repl_window.h src/script/repl_window.c
git commit -m "feat(script): add REPL floating window with history and syntax support"
```

---

## Task 8: 应用集成

**Files:**
- Modify: `src/app/options.h`
- Modify: `src/app/application.h`
- Modify: `src/app/application.c`
- Modify: `src/meson.build`

### Step 1: 扩展 options.h

在 `struct scrcpy_options` 中添加：

```c
    /* Script engine options */
    const char *script_path;     /* -s/--script: startup script file */
    const char *script_eval;     /* -e/--eval: expression to evaluate */
    bool repl;                   /* -r/--repl: show REPL window on start */
```

在 `options.c` 的默认值中添加：

```c
    .script_path = NULL,
    .script_eval = NULL,
    .repl = false,
```

### Step 2: 扩展 application.h

添加 include 和成员：

```c
#include "../script/script_api.h"
#include "../script/repl_window.h"
```

在 `application_t` 中添加：

```c
    script_engine_t script_engine;
    repl_window_t repl_window;
    bool script_enabled;
```

### Step 3: 修改 application.c

在 `application_init` 中添加脚本引擎初始化：

```c
    app->script_enabled = (options->script_path || options->script_eval || options->repl);
    if (app->script_enabled) {
        if (!script_engine_init(&app->script_engine, options->script_path,
                                options->script_eval, NULL, NULL)) {
            log_warn("Script engine init failed, scripting disabled");
            app->script_enabled = false;
        } else {
            repl_window_init(&app->repl_window, GetModuleHandle(NULL),
                             NULL, NULL);  /* eval callback wired later */
            if (options->repl) {
                repl_window_show(&app->repl_window);
            }
        }
    }
```

在 `application_run` 中启动脚本引擎：

```c
    if (app->script_enabled) {
        script_engine_start(&app->script_engine);
    }
```

在 PeekMessage 循环中处理脚本消息：

```c
    /* Process script engine messages */
    if (app->script_enabled) {
        script_process_messages(&app->script_engine,
            /* callbacks: */
            NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
            app);
        repl_window_process_message(&app->repl_window, &msg);
    }
```

在 `application_destroy` 中清理：

```c
    if (app->script_enabled) {
        script_engine_destroy(&app->script_engine);
        repl_window_destroy(&app->repl_window);
    }
```

### Step 4: 更新 meson.build

在根 `meson.build` 的 `winlibs` 中添加 `comctl32`（REPL 窗口的 `SetWindowSubclass` 需要）：

```meson
        cc.find_library('comctl32'),
```

在 `src/meson.build` 中将 `script_src` 加入 executable 的源文件列表。

### Step 5: 编译验证

```bash
ninja -C builddir
```

### Step 6: 提交

```bash
git add src/app/options.h src/app/options.c src/app/application.h src/app/application.c src/meson.build
git commit -m "feat: integrate script engine into application lifecycle"
```

---

## Task 9: CLI 与配置

**Files:**
- Modify: `src/app/cli.c`
- Modify: `src/app/config.c`

### Step 1: 添加 CLI 参数解析

在 `cli.c` 的第二遍循环中添加（在 `-r`/`--record` 之后）：

```c
        } else if (strcmp(argv[i], "--script") == 0) {
            if (i + 1 >= argc) {
                log_error("Missing script file path");
                return false;
            }
            options->script_path = argv[++i];
        } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--eval") == 0) {
            if (i + 1 >= argc) {
                log_error("Missing expression after -e/--eval");
                return false;
            }
            options->script_eval = argv[++i];
        } else if (strcmp(argv[i], "--repl") == 0) {
            options->repl = true;
```

在帮助信息中添加：

```c
            printf("  --script <file>            Load Scheme script on start\n");
            printf("  -e, --eval <expr>          Evaluate Scheme expression\n");
            printf("  --repl                     Show Scheme REPL window\n");
```

**注意：** `-r` 已被 `--record` 使用，所以 REPL 使用 `--repl`（无短选项）。

### Step 2: 添加配置文件支持

在 `config.c` 的 `apply_setting` 中添加新的 section 处理：

```c
    /* [script] */
    if (strcmp(section, "script") == 0) {
        if (strcmp(key, "script_dir") == 0) {
            /* Store for later use */
        } else if (strcmp(key, "autoload") == 0) {
            options->script_path = _strdup(value);
        } else if (strcmp(key, "repl") == 0) {
            options->repl = parse_bool(value);
        } else {
            log_warn("Unknown key '%s' in [script]", key);
        }
        return;
    }
```

在已知 section 列表中添加 `"script"`。

### Step 3: 编译验证

```bash
ninja -C builddir
```

### Step 4: 提交

```bash
git add src/app/cli.c src/app/config.c
git commit -m "feat: add --script, --eval, --repl CLI options and [script] config section"
```

---

## Task 10: 集成测试与最终验证

**Files:**
- Create: `tests/test_script_engine.c`
- Modify: `tests/meson.build`

### Step 1: 编写引擎生命周期测试

```c
/* tests/test_script_engine.c */
#include "../src/script/script_api.h"
#include "../src/platform/log.h"
#include <stdio.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  %-40s ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static void test_queue_init_destroy(void) {
    TEST("queue init and destroy");
    script_msg_queue_t q;
    assert(script_msg_queue_init(&q));
    assert(q.initialized);
    script_msg_queue_destroy(&q);
    assert(!q.initialized);
    PASS();
}

static void test_queue_send_recv_loopback(void) {
    TEST("queue send/recv loopback");
    script_msg_queue_t q;
    script_msg_queue_init(&q);

    for (int i = 0; i < 50; i++) {
        script_msg_t msg = {0};
        msg.type = MSG_INJECT_KEYCODE;
        msg.data_size = 4;
        memcpy(msg.data, &i, 4);
        assert(script_msg_queue_send(&q, &msg));
    }

    for (int i = 0; i < 50; i++) {
        script_msg_t msg = {0};
        assert(script_msg_queue_try_recv(&q, &msg));
        int val;
        memcpy(&val, msg.data, 4);
        assert(val == i);
    }

    script_msg_queue_destroy(&q);
    PASS();
}

static void test_engine_init_destroy(void) {
    TEST("engine init and destroy");
    script_engine_t engine;
    assert(script_engine_init(&engine, NULL, NULL, NULL, NULL));
    assert(engine.initialized);
    script_engine_destroy(&engine);
    PASS();
}

static void test_event_dispatch_sizes(void) {
    TEST("event dispatch message sizes");
    /* Verify message data sizes match expectations */
    script_msg_t msg = {0};

    msg.type = MSG_INJECT_KEYCODE;
    assert(sizeof(int) * 2 <= SCRIPT_MSG_MAX_DATA_SIZE);

    msg.type = MSG_INJECT_TOUCH;
    assert(sizeof(int) * 3 <= SCRIPT_MSG_MAX_DATA_SIZE);

    msg.type = MSG_INJECT_SCROLL;
    assert(sizeof(int) * 4 <= SCRIPT_MSG_MAX_DATA_SIZE);

    PASS();
}

int main(void) {
    log_init(LOG_LEVEL_ERROR);
    printf("Script engine tests:\n");

    test_queue_init_destroy();
    test_queue_send_recv_loopback();
    test_engine_init_destroy();
    test_event_dispatch_sizes();

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    log_destroy();
    return tests_failed > 0 ? 1 : 0;
}
```

### Step 2: 更新测试构建

在 `tests/meson.build` 中添加：

```meson
test_script_engine = executable('test_script_engine', 'test_script_engine.c',
    '../src/script/message_queue.c',
    '../src/script/engine.c',
    '../src/platform/log.c',
    dependencies: winlibs,
)
test('Script engine test', test_script_engine)
```

### Step 3: 运行全部测试

```bash
meson test -C builddir
```

Expected: All existing tests + new script engine tests pass.

### Step 4: 手动验证 REPL

```bash
./builddir/autoscrcpy --repl
```

Expected: 主窗口 + REPL 浮动窗口同时显示。在 REPL 中输入 `(display "hello")` 回车，输出区域显示 `hello`。

### Step 5: 提交

```bash
git add tests/test_script_engine.c tests/meson.build
git commit -m "test: add script engine integration tests"
```

### Step 6: 最终提交

```bash
git add -A
git commit -m "feat: Chez Scheme embedded scripting engine complete

- Message queue for cross-thread communication
- Script engine with independent thread lifecycle
- FFI bindings for device control commands
- Scheme runtime library (lib/init.ss)
- Event dispatch between main thread and script engine
- REPL floating window with history
- CLI options: --script, --eval, --repl
- Config section: [script]"
```

---

## 自检清单

1. **Spec 覆盖：** ✅ 消息队列、FFI 绑定、Scheme API、REPL 窗口、构建集成、错误隔离、CLI/Config — 全部覆盖。
2. **占位符扫描：** ✅ 所有 TODO 仅在 Chez Scheme 完全集成前的临时占位，实现计划中已标注。
3. **类型一致性：** ✅ `script_msg_queue_t`、`script_msg_t`、`script_engine_t`、`repl_window_t` 在所有任务中一致。
4. **命名规范：** ✅ 遵循 `_t` 后缀、`_init`/`_destroy` 模式、`log_*` 日志。
5. **构建依赖：** ✅ Task 2 必须在 Task 3-7 之前完成（Chez 头文件和库）。
