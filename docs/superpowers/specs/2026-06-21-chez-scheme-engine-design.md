# Chez Scheme 嵌入式脚本引擎设计

## 概述

将 Chez Scheme 作为嵌入式脚本引擎集成到 AutoScrcpy，支持三种使用场景：

1. **设备自动化** — 通过脚本编排设备操作序列（自动点击、输入文本、截图等）
2. **运行时扩展** — 用户在运行时通过脚本扩展功能（自定义快捷键、交互模式）
3. **交互式 REPL** — 提供 Scheme REPL，实时查询设备状态、发送控制命令、调试协议

## 约束

- Chez Scheme 静态编译集成，无外部运行时依赖
- 脚本错误不得崩溃主程序（隔离原则）
- D3D11 设备绑定主线程，脚本引擎在独立线程运行
- 遵循现有编码规范：C11、`_t` 后缀类型、`log_*` 日志、`_init`/`_destroy` 模式

## 架构

```
main.c → app/ → script/ → (Chez Scheme runtime)
                  ↓
          message_queue (线程安全)
                  ↓
         ┌───────┼───────┐
         ↓       ↓       ↓
      device/  control/  decode/
```

### 新增模块：`src/script/`

| 文件 | 职责 |
|------|------|
| `engine.h/c` | Chez Scheme 引擎生命周期：初始化、启动线程、关闭 |
| `bindings.h/c` | C→Scheme FFI 绑定：将设备操作注册为 Scheme 外部过程 |
| `message_queue.h/c` | 线程安全消息队列：Scheme 线程 ↔ 主线程通信 |
| `repl_window.h/c` | REPL 浮动窗口：独立 Win32 窗口，文本终端风格 |
| `event_dispatch.h/c` | 事件分发：将主线程事件转发给 Scheme 回调 |
| `script_api.h` | 对外头文件：`script_engine_init/run/destroy` |

### 与现有模块的关系

- `application_t` 新增 `script_engine_t` 成员
- `application_init` 中初始化脚本引擎
- `application_run` 的 PeekMessage 循环中处理消息队列
- `application_destroy` 中关闭脚本引擎
- 控制器事件（key/mouse/wheel）同时分发给脚本引擎

## 消息队列

### 消息类型

```c
typedef enum {
    /* Scheme → Main: 请求执行操作 */
    MSG_INJECT_KEYCODE,      /* {keycode, down} */
    MSG_INJECT_TEXT,          /* {text} */
    MSG_INJECT_TOUCH,         /* {x, y, action, buttons} */
    MSG_INJECT_SCROLL,        /* {x, y, dx, dy} */
    MSG_SET_CLIPBOARD,        /* {text} */
    MSG_EXPAND_NOTIFICATION,
    MSG_COLLAPSE_PANELS,
    MSG_SET_DISPLAY_POWER,    /* {on} */
    MSG_ROTATE_DEVICE,
    MSG_START_APP,            /* {package} */

    /* Main → Scheme: 事件通知 */
    MSG_EVENT_KEY,            /* {vk, down} */
    MSG_EVENT_MOUSE,          /* {x, y, buttons, action} */
    MSG_EVENT_FRAME,          /* {width, height} */
    MSG_EVENT_CONNECTED,
    MSG_EVENT_DISCONNECTED,
    MSG_EVENT_ERROR,          /* {message} */

    /* 控制 */
    MSG_SHUTDOWN,
} script_msg_type_t;
```

### 队列实现

- 固定大小环形缓冲区（256 条消息），`CRITICAL_SECTION` 保护
- 非阻塞发送（满了丢弃并 log_warn）
- 阻塞接收（`WaitForSingleObject` 在信号量上）

### 线程交互流程

1. Scheme 脚本调用 `(inject-keycode HOME #t)`
2. FFI 绑定函数 → 将 `MSG_INJECT_KEYCODE` 放入队列
3. 主线程 PeekMessage 空闲时检查队列 → 取出消息 → 调用 `controller_on_key_event` 等
4. 主线程发生按键事件 → 将 `MSG_EVENT_KEY` 放入 Scheme 队列
5. Scheme 线程收到事件 → 调用已注册的回调函数

## Scheme API（FFI 绑定）

### 控制命令

```scheme
;; 按键注入
(inject-keycode keycode down?)
;; keycode: Scheme 符号 → Android keycode 整数
;; 'home → 3, 'back → 4, 'power → 26, 'menu → 82
;; 'volume-up → 24, 'volume-down → 25, 'enter → 66
;; 'dpad-center → 23, 'dpad-up → 19, 'dpad-down → 20
;; 'dpad-left → 21, 'dpad-right → 22
;; 也可直接传整数（Android keycode 值）
;; down?: #t 按下, #f 松开

;; 文本输入
(inject-text "Hello World")

;; 触摸事件
(inject-touch 500 800 'down)   ;; 坐标为设备像素
(inject-touch 500 800 'move)
(inject-touch 500 800 'up)

;; 滚动
(inject-scroll 500 800 0 -3)   ;; dx, dy

;; 剪贴板
(set-clipboard "text")
(get-clipboard)                 ;; 返回字符串

;; 通知面板
(expand-notification)
(expand-settings)
(collapse-panels)

;; 电源
(set-display-power #t)         ;; #t 开, #f 关
(rotate-device)

;; 启动应用
(start-app "com.example.app")
```

### 设备状态查询

```scheme
(device-width)                  ;; 返回整数
(device-height)                 ;; 返回整数
(device-name)                   ;; 返回字符串
(is-connected?)                 ;; 返回 #t/#f
(video-size)                    ;; 返回 '(width . height)
(window-size)                   ;; 返回 '(width . height)
```

### 事件回调注册

```scheme
(on-key (lambda (vk down?)
  (format #t "Key: ~a ~a~%" vk down?)))

(on-mouse (lambda (x y buttons action)
  (format #t "Mouse: ~a,~a ~a~%" x y action)))

(on-frame (lambda (width height)
  (format #t "Frame: ~ax~a~%" width height)))

(on-connect (lambda ()
  (display "Connected!\n")))

(on-disconnect (lambda ()
  (display "Disconnected!\n")))
```

### 视频帧访问

```scheme
;; 获取当前帧的 NV12 像素数据（返回 bytevector 深拷贝，不影响渲染管线）
;; 若无可用帧返回 #f
(capture-frame)                 ;; 返回 #vu8(...) 或 #f

(frame-width)
(frame-height)
```

### 内置辅助过程

```scheme
(sleep-ms 1000)                 ;; 延迟（毫秒）
(log-info "message")            ;; 日志
(log-error "message")
(log-debug "message")
(load-script "path/to/file.scm") ;; 加载脚本文件
```

## REPL 浮动窗口

### 窗口规格

- 独立 Win32 窗口（`CreateWindowEx`），不依附主窗口
- 默认大小 600×400，可调整
- 标题："AutoScrcpy Scheme REPL"
- 样式：`WS_OVERLAPPEDWINDOW`

### UI 组件

- **输出区域**：`EDIT` 控件（`ES_MULTILINE | ES_READONLY | WS_VSCROLL`）
- **输入行**：底部 `EDIT` 控件，单行，回车执行
- 上下箭头翻阅历史命令

### 行为

- 输入表达式 → 回车 → 发送到 Scheme 引擎执行 → 结果显示在输出区域
- 错误信息以 `*** Error:` 前缀显示
- REPL 窗口可独立关闭/重新打开（不影响脚本引擎运行）
- 快捷键 Ctrl+Shift+R 切换显示

### 启动行为

- 默认不显示 REPL 窗口
- `--repl` 参数或脚本中调用 `(open-repl)` 时显示
- `--script file.scm` 加载脚本，REPL 窗口同时可用
- `log_info` 等日志同时输出到 REPL 窗口

## 构建集成

### Chez Scheme 作为 Meson Subproject

Chez Scheme 使用自己的 `configure` + `make` 构建系统，不直接支持 Meson。集成方案：

1. `subprojects/chez-scheme.wrap` 指向 Chez Scheme Git 仓库
2. `subprojects/packagefiles/chez-scheme/meson.build` 包装构建过程
3. 使用 `custom_target` 调用 Chez 的 configure + make
4. 导出 `chez_scheme_dep` 供主项目使用

### 嵌入式初始化

```c
#include "scheme.h"

static void scheme_init_engine(void) {
    Sscheme_init(false);
    Sbuild_heap(NULL, NULL);

    /* 注册外部过程 */
    Sforeign_symbol("c_inject_keycode", (void *)c_inject_keycode);
    Sforeign_symbol("c_inject_text", (void *)c_inject_text);
    /* ... 更多绑定 ... */

    /* 加载 Scheme 运行时库 */
    Sapply(0, Stop_level_value(Sstring_to_symbol("load")),
           Sstring("lib/init.ss"));
}
```

### Scheme 运行时库 (`lib/init.ss`)

提供用户友好的 Scheme API，内部调用 C FFI：

```scheme
(define (inject-keycode keycode down?)
  (let ((code (case keycode
                ((home) 3) ((back) 4) ((power) 26)
                ((volume-up) 24) ((volume-down) 25)
                (else keycode))))
    (c-inject-keycode code (if down? 1 0))))

(define (inject-touch x y action)
  (let ((act (case action
               ((down) 0) ((up) 1) ((move) 2)
               (else action))))
    (c-inject-touch x y act)))
```

## 错误处理与隔离

### 隔离原则

脚本错误绝不应崩溃主程序。

### 错误捕获

Scheme 线程中执行脚本时使用 `Ssetjmp` 保护，异常被捕获后显示在 REPL 并继续运行。

### 错误分类

| 错误类型 | 处理方式 |
|----------|----------|
| Scheme 语法错误 | REPL 显示错误信息，继续运行 |
| 运行时异常 | REPL 显示错误和堆栈，继续运行 |
| FFI 调用失败（设备断连等） | 返回 `#f`，脚本可检查 |
| Scheme 线程崩溃 | 记录日志，主线程继续运行，可重启引擎 |
| 主线程操作失败 | 通过消息队列通知 Scheme 线程 |

### 资源限制

- 消息队列满时丢弃新消息并 log_warn（非阻塞）
- 帧数据访问返回快照，不影响渲染管线
- 脚本不能直接操作 D3D11 资源（必须通过消息队列）

## 命令行与配置

### 新增命令行参数

```
-s, --script <file.scm>    启动时加载并执行 Scheme 脚本
-r, --repl                 启动时显示 REPL 窗口
-e, --eval <expression>    启动时执行单个 Scheme 表达式
```

### 新增配置文件选项

```ini
[script]
script_dir=./scripts
autoload=init.scm
repl=false
```

### 使用示例

```bash
./autoscrcpy -s automation.scm       # 启动并加载脚本
./autoscrcpy --repl                   # 启动并打开 REPL
./autoscrcpy -e '(display "Hello\n")' # 执行表达式
./autoscrcpy -s test.scm --repl       # 脚本 + REPL
```

## 测试策略

### 单元测试

| 测试文件 | 覆盖内容 |
|----------|----------|
| `test_message_queue.c` | 队列的发送/接收/满载/并发安全 |
| `test_bindings.c` | FFI 绑定的参数转换和返回值 |
| `test_script_eval.c` | Scheme 表达式求值、错误捕获 |

### 集成测试

| 测试文件 | 覆盖内容 |
|----------|----------|
| `test_script_engine.c` | 引擎生命周期：init → load script → run → destroy |
| `test_repl_window.c` | REPL 窗口创建、输入输出、关闭 |

### 手动测试

| 场景 | 验证点 |
|------|--------|
| 自动化脚本 | 脚本执行按键/触摸序列，设备正确响应 |
| REPL 交互 | 输入表达式，正确显示结果和错误 |
| 脚本错误恢复 | 脚本出错后 REPL 继续可用 |
| 设备断连 | 脚本收到断连通知，不崩溃 |

## 文件清单

### 新增文件

```
src/script/
├── engine.h              # 引擎生命周期 API
├── engine.c              # 引擎实现
├── bindings.h            # FFI 绑定声明
├── bindings.c            # FFI 绑定实现
├── message_queue.h       # 消息队列 API
├── message_queue.c       # 消息队列实现
├── repl_window.h         # REPL 窗口 API
├── repl_window.c         # REPL 窗口实现
├── event_dispatch.h      # 事件分发 API
├── event_dispatch.c      # 事件分发实现
└── script_api.h          # 对外统一头文件

lib/
└── init.ss               # Scheme 运行时库

subprojects/
├── chez-scheme.wrap      # Meson wrap 文件
└── packagefiles/
    └── chez-scheme/
        └── meson.build   # Chez Scheme 构建包装

tests/
├── test_message_queue.c
├── test_bindings.c
├── test_script_eval.c
├── test_script_engine.c
└── test_repl_window.c
```

### 修改文件

| 文件 | 修改内容 |
|------|----------|
| `src/app/application.h` | 新增 `script_engine_t` 成员 |
| `src/app/application.c` | 初始化/销毁脚本引擎，PeekMessage 中处理消息队列 |
| `src/app/cli.c` | 解析 `--script`/`--repl`/`--eval` 参数 |
| `src/app/options.h` | 新增脚本相关选项字段 |
| `src/app/config.c` | 读取 `[script]` 配置节 |
| `src/meson.build` | 添加 `src/script/` 源文件 |
| `meson.build` | 添加 `chez_scheme_dep` 依赖 |
