# Config File Design

**Date:** 2026-06-21
**Status:** Approved
**Scope:** INI 配置文件支持，替代纯命令行参数启动

## Overview

AutoScrcpy 当前仅支持命令行参数配置。本设计新增 INI 配置文件支持，用户可通过 `-c config.ini` 指定配置文件，实现参数持久化和简化启动命令。

**优先级规则：** CLI > 配置文件 > 默认值

## Module Architecture

新增 `config` 模块，位于 `src/app/config.h` 和 `src/app/config.c`，与 `cli.h` 同级。

```
src/app/
├── cli.c          # 命令令行解析（已有）
├── cli.h
├── config.c       # INI 配置文件解析（新增）
├── config.h
├── options.c      # 选项默认值（已有）
├── options.h       # scrcpy_options 结构体（扩展）
├── application.c
└── ...
```

### Data Flow

```
main.c 启动
  ├─ 1. 初始化 scrcpy_options = 默认值
  ├─ 2. cli_parse() 扫描 argv，若发现 -c <file> 则记录 config_path
  ├─ 3. 若 config_path 存在 → config_parse(config_path, &options)
  ├─ 4. cli_parse() 继续解析其余 CLI 参数，覆盖 config 值
  └─ 5. application_init(&app, &options)
```

CLI 解析分两轮：第一轮找 `-c`，加载配置文件；第二轮覆盖配置值。保持 `CLI > 配置 > 默认` 的优先级。

## INI Parser

`config.c` 中实现简单的 INI 解析器，逐行读取文件，识别三种语法：

```ini
# 注释行（以 # 开头，忽略）
[section]           ← 切换当前 section
key = value         ← 设置当前 section 下的键值
```

### Parse Flow

```c
bool config_parse(const char *path, struct scrcpy_options *options);
```

1. `fopen(path)` 打开文件，失败返回 `false`
2. 逐行 `fgets()` 读取，跳过空行和 `#` 注释
3. 遇到 `[xxx]` → 记录当前 section 名
4. 遇到 `key = value` → 根据 section + key 调用对应的赋值函数
5. 未知 section/key → `log_warn()` 警告但继续
6. 关闭文件，返回 `true`

### Value Type Handling

- **字符串：** 直接赋值（`serial`, `video_codec`, `audio_codec`, `server_path`, `window_title`）
- **整数：** `atoi()` 转换（`port`, `max_size`, `video_bit_rate`, `audio_bit_rate`）
- **布尔：** `"true"/"1"/"yes"` → `true`，`"false"/"0"/"no"` → `false`

### Error Handling

- 文件不存在 → `log_error()` + 返回 `false`
- 语法错误（如缺少 `]`）→ `log_warn()` + 跳过该行
- 未知 key → `log_warn()` + 跳过

## Config File Format

完整配置文件示例：

```ini
# AutoScrcpy 配置文件

[connection]
serial = 192.168.1.100:5555
port = 5555
server_path = scrcpy-server.jar

[video]
enabled = true
codec = h264
max_size = 1080
bit_rate = 8000000

[audio]
enabled = true
codec = opus
bit_rate = 128000
source = output

[control]
enabled = true

[window]
title = AutoScrcpy
fullscreen = false
always_on_top = false
width = 0
height = 0

[device]
turn_screen_off = false
stay_awake = false
show_touches = false

[record]
enabled = false
filename =

[log]
level = info
```

## Options Structure Extension

`scrcpy_options` 结构体新增 3 个字段：

```c
struct scrcpy_options {
    // 现有字段（保持不变）
    const char *serial;
    const char *server_path;
    const char *record_filename;
    const char *window_title;
    uint16_t port;
    uint32_t max_size;
    uint32_t video_bit_rate;
    uint32_t audio_bit_rate;
    const char *video_codec;
    const char *audio_codec;
    bool control;
    bool video;
    bool audio;
    bool fullscreen;
    bool always_on_top;
    bool turn_screen_off;
    bool stay_awake;
    bool show_touches;
    bool record;

    // 新增字段
    const char *audio_source;    // "output" 或 "mic"
    uint32_t window_width;       // 初始窗口宽度（0 = 自动）
    uint32_t window_height;      // 初始窗口高度（0 = 自动）
    int log_level;               // LOG_LEVEL_DEBUG/INFO/WARN/ERROR
};
```

## INI Key to Struct Field Mapping

| Section | Key | Struct 字段 | 类型 |
|---------|-----|-------------|------|
| connection | serial | serial | string |
| connection | port | port | uint16 |
| connection | server_path | server_path | string |
| video | enabled | video | bool |
| video | codec | video_codec | string |
| video | max_size | max_size | uint32 |
| video | bit_rate | video_bit_rate | uint32 |
| audio | enabled | audio | bool |
| audio | codec | audio_codec | string |
| audio | bit_rate | audio_bit_rate | uint32 |
| audio | source | audio_source | string |
| control | enabled | control | bool |
| window | title | window_title | string |
| window | fullscreen | fullscreen | bool |
| window | always_on_top | always_on_top | bool |
| window | width | window_width | uint32 |
| window | height | window_height | uint32 |
| device | turn_screen_off | turn_screen_off | bool |
| device | stay_awake | stay_awake | bool |
| device | show_touches | show_touches | bool |
| record | enabled | record | bool |
| record | filename | record_filename | string |
| log | level | log_level | int |

## CLI Changes

### New Parameter

```
-c, --config <file>    指定配置文件路径
```

### cli_parse() Refactoring

改造为两轮扫描：

```c
bool cli_parse(int argc, char *argv[], struct scrcpy_options *options) {
    *options = scrcpy_options_default;

    // 第一轮：找 -c 参数
    const char *config_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) { log_error("Missing config path"); return false; }
            config_path = argv[++i];
        }
    }

    // 加载配置文件（如果指定）
    if (config_path) {
        if (!config_parse(config_path, options)) {
            log_error("Failed to load config: %s", config_path);
            return false;
        }
    }

    // 第二轮：CLI 参数覆盖
    for (int i = 1; i < argc; i++) {
        // 现有的参数解析逻辑（跳过 -c 及其值）
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            i++; // 跳过 config path 值
            continue;
        }
        // ... 其余参数解析不变 ...
    }
    return true;
}
```

### Help Text Update

```
  -c, --config <file>        Load config file
```

### main.c

无需修改。`cli_parse()` 内部处理配置文件加载。

## Testing

### Unit Tests: test_config.c

新增自动测试，覆盖：

| 用例 | 描述 |
|------|------|
| 解析完整配置 | 所有 section + key 正确解析 |
| 布尔值变体 | `true/1/yes` 和 `false/0/no` 都正确 |
| 注释和空行 | `#` 注释和空行被忽略 |
| 未知 section | `log_warn` 但不失败 |
| 未知 key | `log_warn` 但不失败 |
| 文件不存在 | 返回 `false` |
| 部分配置 | 只设置部分参数，其余保持默认 |
| 值覆盖 | CLI 覆盖 config 值的优先级测试 |

### Error Scenarios

| 场景 | 行为 |
|------|------|
| `-c` 未指定且无默认配置 | 正常启动，使用默认值 |
| `-c` 指定但文件不存在 | `log_error` + 退出 |
| 配置文件语法错误 | `log_warn` + 跳过该行，继续解析 |
| 未知 section/key | `log_warn` + 跳过 |
| 值类型错误（如 port=abc） | `log_warn` + 使用默认值 |

### Log Level Parsing

```c
static int parse_log_level(const char *str) {
    if (strcmp(str, "debug") == 0) return LOG_LEVEL_DEBUG;
    if (strcmp(str, "info") == 0)  return LOG_LEVEL_INFO;
    if (strcmp(str, "warn") == 0)  return LOG_LEVEL_WARN;
    if (strcmp(str, "error") == 0) return LOG_LEVEL_ERROR;
    log_warn("Unknown log level: %s, using info", str);
    return LOG_LEVEL_INFO;
}
```

## Files to Create/Modify

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/app/config.h` | 新增 | config_parse() 声明 |
| `src/app/config.c` | 新增 | INI 解析器实现 |
| `src/app/options.h` | 修改 | 新增 audio_source, window_width, window_height, log_level 字段 |
| `src/app/options.c` | 修改 | 新增字段默认值 |
| `src/app/cli.c` | 修改 | 两轮扫描 + -c 参数 + 帮助文本 |
| `tests/test_config.c` | 新增 | 配置文件解析单元测试 |
| `tests/meson.build` | 修改 | 注册 test_config 测试 |
| `meson.build` | 修改 | 注册 config.c 源文件 |

## Non-Goals

- 不支持多 profile 配置
- 不支持配置文件嵌套/包含
- 不支持环境变量替换
- 不自动搜索配置文件（必须通过 `-c` 显式指定）
