# Mbedtls 构建管理设计规范

**日期：** 2026-06-21
**状态：** 已批准
**作者：** Claude Code

## 1. 问题陈述

### 1.1 背景

AutoScrcpy 项目使用 Meson 构建系统，依赖 mbedtls 库进行 ADB TLS 握手。mbedtls 通过 `subprojects/mbedtls.wrap` 配置，使用 `wrap-git` 方式从 GitHub 克隆。

### 1.2 问题

- `subprojects/mbedtls/meson.build` 是**自定义文件**（上游 mbedtls 没有 meson.build）
- 当前 `.gitignore` 忽略 `subprojects/*/` 目录，导致此文件不被 Git 跟踪
- 新环境 clone 后，运行 `meson setup` 会克隆 mbedtls 仓库，但缺少自定义的 `meson.build`
- 构建失败：Meson 找不到构建定义

### 1.3 根本原因

- 上游 mbedtls 不提供 Meson 构建支持
- 自定义构建文件与克隆的源码在同一目录，被 `.gitignore` 忽略
- 缺少将自定义文件自动注入到克隆源码的机制

## 2. 解决方案

### 2.1 方案选择

使用 Meson 原生支持的 `patch_directory` 机制。

**备选方案：**
- ❌ 直接提交到 Git：目录在 `meson setup` 前不存在，会被覆盖
- ❌ Patch 文件机制：对单个文件过度工程化
- ❌ Wrap redirect + 独立仓库：增加外部依赖，过度工程化

### 2.2 patch_directory 机制

**工作原理：**
1. Meson 读取 `.wrap` 文件中的 `patch_directory` 配置
2. 执行 `git clone` 或下载源码
3. 将 `patch_directory` 中的文件复制到克隆的源码目录
4. 覆盖已存在的文件，添加不存在的文件

**优势：**
- ✅ Meson 原生支持，无需额外脚本或工具
- ✅ 自定义文件提交到 Git，版本管理清晰
- ✅ 新环境自动工作，零额外步骤
- ✅ 符合 Meson 社区最佳实践

## 3. 详细设计

### 3.1 目录结构

```
subprojects/
├── mbedtls.wrap              # 修改：添加 patch_directory
├── packagefiles/             # 新增：提交到 Git
│   └── mbedtls/              # 新增：提交到 Git
│       └── meson.build       # 自定义构建文件
├── ffmpeg.wrap               # 不变
└── ...                       # 其他文件不变
```

### 3.2 文件变更

#### 3.2.1 `subprojects/mbedtls.wrap`（修改）

**变更前：**
```ini
[wrap-git]
url = https://github.com/Mbed-TLS/mbedtls.git
revision = v3.6.2
depth = 1

[provide]
mbedtls = mbedtls_dep
```

**变更后：**
```ini
[wrap-git]
url = https://github.com/Mbed-TLS/mbedtls.git
revision = v3.6.2
depth = 1
patch_directory = mbedtls

[provide]
mbedtls = mbedtls_dep
```

**新增行：** `patch_directory = mbedtls`

#### 3.2.2 `subprojects/packagefiles/mbedtls/meson.build`（新增）

**内容：** 从 `subprojects/mbedtls/meson.build` 移动（128 行）

**关键内容：**
- 项目定义：`project('mbedtls', 'c', version: '3.6.2', license: 'Apache-2.0')`
- 源文件列表：98 个 `.c` 文件
- 静态库构建：`static_library('mbedtls', ...)`
- 依赖声明：`declare_dependency(...)`

#### 3.2.3 `.gitignore`（修改）

**变更前：**
```gitignore
# Subprojects
subprojects/*/
!subprojects/*.wrap
```

**变更后：**
```gitignore
# Subprojects
subprojects/*/
!subprojects/*.wrap
!subprojects/packagefiles/
```

**新增行：** `!subprojects/packagefiles/`

### 3.3 清理操作

- 删除 `subprojects/mbedtls/meson.build`（已移动到 packagefiles）
- 或者：保留原文件，但会被 patch_directory 覆盖（无害）

## 4. 构建流程

### 4.1 新环境构建步骤

```bash
# 1. 克隆项目
git clone <repo-url>
cd autoscrcpy

# 2. 配置（自动触发 subprojects 下载）
meson setup builddir --native-file meson-native-clang-gcc.ini

# 3. 构建
ninja -C builddir
```

### 4.2 Meson 内部自动执行

1. 读取 `subprojects/mbedtls.wrap`
2. 检测 `subprojects/mbedtls/` 目录不存在
3. 执行 `git clone https://github.com/Mbed-TLS/mbedtls.git -b v3.6.2 --depth=1`
4. 将 `subprojects/packagefiles/mbedtls/` 中的文件复制到克隆的目录
5. 覆盖/添加 `meson.build`
6. 继续构建流程

### 4.3 与现有机制一致性

- FFmpeg 使用 `wrap-git`，其 `meson.build` 来自 meson-ports fork（已包含）
- mbedtls 使用 `wrap-git` + `patch_directory`，其 `meson.build` 来自 packagefiles
- 两者都通过 `.wrap` 文件管理，用户无需关心细节

## 5. 错误处理

### 5.1 patch_directory 目录不存在

**错误：** `Subproject patch_directory not found`

**处理：** 确保 `packagefiles/mbedtls/` 目录正确提交到 Git

### 5.2 packagefiles 中的文件与上游冲突

**行为：** `patch_directory` 会覆盖已存在的文件

**处理：** 这是预期行为，我们的 `meson.build` 会覆盖任何同名文件

### 5.3 mbedtls 版本升级

**步骤：**
1. 修改 `mbedtls.wrap` 中的 `revision = v3.7.0`
2. 检查 `packagefiles/mbedtls/meson.build` 是否需要更新
3. 可能需要更新源文件列表（如果有新增/删除的 .c 文件）
4. 测试构建和功能

### 5.4 subprojects 目录已存在（旧环境）

**场景：** 如果 `subprojects/mbedtls/` 已存在，Meson 不会重新克隆

**处理：**
- 删除旧目录：`rm -rf subprojects/mbedtls/`
- 或运行：`meson subprojects update`
- 或重新配置：`meson setup --wipe builddir`

## 6. 测试策略

### 6.1 自动测试

```bash
meson test -C builddir
```

- 38 个自动测试用例应全部通过
- 不受影响（构建系统变更不影响功能）

### 6.2 集成测试（新环境模拟）

```bash
# 模拟新环境
rm -rf subprojects/mbedtls/
rm -rf builddir/

# 重新配置和构建
meson setup builddir --native-file meson-native-clang-gcc.ini
ninja -C builddir

# 验证
meson test -C builddir
```

### 6.3 设备测试（可选）

```bash
./builddir/tests/test_device.exe <serial>
```

- 验证完整功能链（ADB → 解码 → 渲染）

## 7. 维护指南

### 7.1 升级 mbedtls 版本

1. 修改 `subprojects/mbedtls.wrap`：
   ```ini
   revision = v3.7.0
   ```
2. 检查上游变更：
   - 新增/删除的源文件
   - 构建选项变更
   - API 变更
3. 更新 `subprojects/packagefiles/mbedtls/meson.build`（如需要）
4. 测试构建和功能
5. 提交变更

### 7.2 修改自定义构建文件

1. 编辑 `subprojects/packagefiles/mbedtls/meson.build`
2. 清理构建目录：`rm -rf builddir/`
3. 重新配置和构建
4. 测试功能
5. 提交变更

## 8. 风险和缓解

### 8.1 风险：mbedtls 上游添加 Meson 支持

**影响：** 我们的自定义 `meson.build` 可能与上游冲突

**缓解：** 
- 定期检查上游变更
- 如果上游添加 Meson 支持，考虑移除 patch_directory

### 8.2 风险：源文件列表过时

**影响：** 构建失败或功能缺失

**缓解：**
- 升级版本时检查源文件列表
- 参考上游 `CMakeLists.txt` 中的源文件列表

### 8.3 风险：构建选项不兼容

**影响：** 编译错误或功能异常

**缓解：**
- 参考上游构建选项
- 测试所有相关功能

## 9. 总结

使用 Meson 的 `patch_directory` 机制管理 mbedtls 的自定义构建文件，解决了新环境中构建文件丢失的问题。此方案：

- ✅ 使用 Meson 原生支持，无需额外工具
- ✅ 新环境自动工作，零额外步骤
- ✅ 版本管理清晰，便于维护
- ✅ 符合社区最佳实践
- ✅ 支持依赖升级和长期维护

**实施工作量：** 小（修改 3 个文件，移动 1 个文件）
**风险：** 低（使用标准机制，无外部依赖）
**收益：** 高（解决构建问题，提升开发体验）
