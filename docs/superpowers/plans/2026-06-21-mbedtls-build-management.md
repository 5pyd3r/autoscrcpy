# Mbedtls 构建管理实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 使用 Meson 的 `patch_directory` 机制管理 mbedtls 的自定义构建文件，确保新环境能正常编译。

**Architecture:** 修改 `.wrap` 文件添加 `patch_directory` 配置，将自定义 `meson.build` 移动到 `packagefiles/` 目录并提交到 Git。Meson 在克隆源码后会自动将 `packagefiles/` 中的文件覆盖到源码目录。

**Tech Stack:** Meson build system, Git, mbedtls 3.6.2

---

## 文件结构

在开始实现前，了解需要创建/修改的文件：

| 文件 | 操作 | 职责 |
|------|------|------|
| `subprojects/packagefiles/mbedtls/meson.build` | 创建 | 存储自定义构建文件（提交到 Git） |
| `subprojects/mbedtls.wrap` | 修改 | 添加 `patch_directory` 配置 |
| `.gitignore` | 修改 | 允许 `packagefiles/` 目录被 Git 跟踪 |
| `subprojects/mbedtls/meson.build` | 删除 | 已移动到 packagefiles 目录 |

---

### Task 1: 创建 packagefiles 目录结构

**Files:**
- Create: `subprojects/packagefiles/mbedtls/meson.build`

- [ ] **Step 1: 创建 packagefiles 目录**

```bash
mkdir -p subprojects/packagefiles/mbedtls
```

- [ ] **Step 2: 复制自定义 meson.build 到 packagefiles**

```bash
cp subprojects/mbedtls/meson.build subprojects/packagefiles/mbedtls/meson.build
```

- [ ] **Step 3: 验证文件已复制**

```bash
ls -la subprojects/packagefiles/mbedtls/
cat subprojects/packagefiles/mbedtls/meson.build | head -5
```

Expected output:
```
total XX
drwxr-xr-x  1 user  staff  XX Jun 21 XX:XX .
drwxr-xr-x  1 user  staff  XX Jun 21 XX:XX ..
-rw-r--r--  1 user  staff  XXXX Jun 21 XX:XX meson.build
project('mbedtls', 'c',
    version: '3.6.2',
    license: 'Apache-2.0',
)
```

- [ ] **Step 4: 提交 packagefiles 目录**

```bash
git add subprojects/packagefiles/
git commit -m "feat: add mbedtls packagefiles for patch_directory mechanism"
```

---

### Task 2: 修改 mbedtls.wrap 文件

**Files:**
- Modify: `subprojects/mbedtls.wrap`

- [ ] **Step 1: 查看当前 mbedtls.wrap 内容**

```bash
cat subprojects/mbedtls.wrap
```

Expected output:
```ini
[wrap-git]
url = https://github.com/Mbed-TLS/mbedtls.git
revision = v3.6.2
depth = 1

[provide]
mbedtls = mbedtls_dep
```

- [ ] **Step 2: 添加 patch_directory 配置**

```bash
cat > subprojects/mbedtls.wrap << 'EOF'
[wrap-git]
url = https://github.com/Mbed-TLS/mbedtls.git
revision = v3.6.2
depth = 1
patch_directory = mbedtls

[provide]
mbedtls = mbedtls_dep
EOF
```

- [ ] **Step 3: 验证修改**

```bash
cat subprojects/mbedtls.wrap
```

Expected output:
```ini
[wrap-git]
url = https://github.com/Mbed-TLS/mbedtls.git
revision = v3.6.2
depth = 1
patch_directory = mbedtls

[provide]
mbedtls = mbedtls_dep
```

- [ ] **Step 4: 提交修改**

```bash
git add subprojects/mbedtls.wrap
git commit -m "feat: add patch_directory to mbedtls.wrap"
```

---

### Task 3: 修改 .gitignore 文件

**Files:**
- Modify: `.gitignore`

- [ ] **Step 1: 查看当前 .gitignore 内容**

```bash
cat .gitignore
```

Expected output (相关部分):
```gitignore
# Subprojects
subprojects/*/
!subprojects/*.wrap

# Log files
runlog.txt
*.log
debug*.txt
```

- [ ] **Step 2: 添加 packagefiles 例外规则**

```bash
cat > .gitignore << 'EOF'
# Build directories
builddir/
build/

# IDE files
.vscode/
.idea/
*.swp
*.swo

# Compiled files
*.o
*.obj
*.exe
*.dll
*.so
*.dylib

# Subprojects
subprojects/*/
!subprojects/*.wrap
!subprojects/packagefiles/

# Log files
runlog.txt
*.log
debug*.txt
EOF
```

- [ ] **Step 3: 验证修改**

```bash
cat .gitignore | grep -A 3 "# Subprojects"
```

Expected output:
```gitignore
# Subprojects
subprojects/*/
!subprojects/*.wrap
!subprojects/packagefiles/
```

- [ ] **Step 4: 提交修改**

```bash
git add .gitignore
git commit -m "feat: allow packagefiles directory in .gitignore"
```

---

### Task 4: 清理旧的 meson.build 文件

**Files:**
- Delete: `subprojects/mbedtls/meson.build`

- [ ] **Step 1: 确认旧文件存在**

```bash
ls -la subprojects/mbedtls/meson.build
```

Expected output:
```
-rw-r--r--  1 user  staff  XXXX Jun XX XX:XX subprojects/mbedtls/meson.build
```

- [ ] **Step 2: 删除旧文件**

```bash
rm subprojects/mbedtls/meson.build
```

- [ ] **Step 3: 验证文件已删除**

```bash
ls subprojects/mbedtls/meson.build 2>&1
```

Expected output:
```
ls: cannot access 'subprojects/mbedtls/meson.build': No such file or directory
```

- [ ] **Step 4: 提交删除**

```bash
git add -A subprojects/mbedtls/
git commit -m "chore: remove old meson.build from mbedtls directory"
```

---

### Task 5: 验证构建系统工作

**Files:**
- None (验证步骤)

- [ ] **Step 1: 清理旧的构建目录**

```bash
rm -rf builddir/
```

- [ ] **Step 2: 清理旧的 mbedtls 目录（模拟新环境）**

```bash
rm -rf subprojects/mbedtls/
```

- [ ] **Step 3: 重新配置项目**

```bash
meson setup builddir --native-file meson-native-clang-gcc.ini
```

Expected output (关键部分):
```
The Meson build system
Version: X.X.X
Source dir: /path/to/autoscrcpy
Build dir: /path/to/autoscrcpy/builddir
Build type: native build
...
Found CMake: ...
...
mbedtls| Generating build files...
...
Build targets in project: XX
```

- [ ] **Step 4: 构建项目**

```bash
ninja -C builddir
```

Expected output:
```
[1/XX] Compiling C object subprojects/mbedtls/libmbedtls.a.p/...
[2/XX] Compiling C object subprojects/mbedtls/libmbedtls.a.p/...
...
[XX/XX] Linking target ...
```

- [ ] **Step 5: 运行自动测试**

```bash
meson test -C builddir
```

Expected output:
```
1/38 test_binary        OK
2/38 test_input_transform OK
...
38/38 test_adb          OK

Full log written to ...
```

- [ ] **Step 6: 验证 mbedtls 库已正确构建**

```bash
ls -la builddir/subprojects/mbedtls/libmbedtls.a
```

Expected output:
```
-rw-r--r--  1 user  staff  XXXXXX Jun 21 XX:XX builddir/subprojects/mbedtls/libmbedtls.a
```

---

### Task 6: 最终提交和验证

**Files:**
- None (最终验证)

- [ ] **Step 1: 查看所有变更**

```bash
git status
git log --oneline -5
```

Expected output:
```
On branch master
nothing to commit, working tree clean

XXXXXXXX chore: remove old meson.build from mbedtls directory
XXXXXXXX feat: allow packagefiles directory in .gitignore
XXXXXXXX feat: add patch_directory to mbedtls.wrap
XXXXXXXX feat: add mbedtls packagefiles for patch_directory mechanism
XXXXXXXX docs: add mbedtls build management design spec
```

- [ ] **Step 2: 验证 packagefiles 目录结构**

```bash
tree subprojects/packagefiles/
```

Expected output:
```
subprojects/packagefiles/
└── mbedtls
    └── meson.build

1 directory, 1 file
```

- [ ] **Step 3: 验证 .wrap 文件内容**

```bash
cat subprojects/mbedtls.wrap
```

Expected output:
```ini
[wrap-git]
url = https://github.com/Mbed-TLS/mbedtls.git
revision = v3.6.2
depth = 1
patch_directory = mbedtls

[provide]
mbedtls = mbedtls_dep
```

- [ ] **Step 4: 验证 .gitignore 规则**

```bash
git check-ignore -v subprojects/packagefiles/mbedtls/meson.build
```

Expected output (应该没有输出，表示文件不被忽略):
```
(空)
```

- [ ] **Step 5: 验证旧目录被忽略**

```bash
git check-ignore -v subprojects/mbedtls/
```

Expected output:
```
.gitignore:XX:subprojects/*/	subprojects/mbedtls/
```

---

## 完成检查清单

实施完成后，确认以下所有项目：

- [ ] `subprojects/packagefiles/mbedtls/meson.build` 存在且内容正确
- [ ] `subprojects/mbedtls.wrap` 包含 `patch_directory = mbedtls`
- [ ] `.gitignore` 包含 `!subprojects/packagefiles/`
- [ ] `subprojects/mbedtls/meson.build` 已删除
- [ ] 项目可以在新环境（无 builddir、无 mbedtls 目录）成功构建
- [ ] 所有 38 个自动测试通过
- [ ] 所有变更已提交到 Git

---

## 故障排除

### 问题：Meson 报错 "Subproject patch_directory not found"

**原因：** `packagefiles/mbedtls/` 目录不存在或不包含 `meson.build`

**解决：**
```bash
ls -la subprojects/packagefiles/mbedtls/
# 如果目录不存在，重新创建
mkdir -p subprojects/packagefiles/mbedtls
cp <backup>/meson.build subprojects/packagefiles/mbedtls/
```

### 问题：构建失败，找不到 mbedtls 源文件

**原因：** `meson.build` 中的源文件列表与 mbedtls 版本不匹配

**解决：**
1. 检查 `subprojects/mbedtls.wrap` 中的 `revision`
2. 对比 `subprojects/packagefiles/mbedtls/meson.build` 中的源文件列表
3. 参考上游 `CMakeLists.txt` 更新源文件列表

### 问题：旧环境构建失败

**原因：** `subprojects/mbedtls/` 目录已存在，Meson 不会重新克隆

**解决：**
```bash
# 方案 1：删除旧目录
rm -rf subprojects/mbedtls/
rm -rf builddir/
meson setup builddir --native-file meson-native-clang-gcc.ini

# 方案 2：使用 meson subprojects
meson subprojects update
meson setup --wipe builddir
```
