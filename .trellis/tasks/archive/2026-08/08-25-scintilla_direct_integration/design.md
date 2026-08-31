# 设计: M06 Scintilla 直接集成原型

## 已验证事实

- Scintilla 本体使用 HPND 许可证（Scientific Toolworks 自由软件许可证），非 GPL
- QScintilla（Qt 绑定）使用 GPL-3.0，项目规则禁止默认采用
- `diegoiast/scintilla-code` 仓库的 `cmake-support` 分支提供 CMake 构建支持
- 该仓库提供 `scintilla-qt.cmake`，可构建 `scintilla-qt-edit`（完整）和基础变体
- `ScintillaEditBase` 类继承自 `QAbstractScrollArea`，提供完整 Qt 事件处理
- `ScintillaEditBase` 继承自 `ScintillaQt`（内部引擎），直接处理键盘、鼠标、IME 等事件
- 项目使用 Qt6 + CMake + FetchContent 管理依赖（已有 md4qt 和 quickjs 先例）

## 方案

### 依赖管理

使用 FetchContent 拉取 `diegoiast/scintilla-code` 仓库的 `cmake-support` 分支，包含其提供的 `scintilla-qt.cmake` 来构建 Scintilla。

**仓库信息：**
- 地址：`https://github.com/diegoiast/scintilla-code.git`
- 分支：`cmake-support`
- 关键文件：`scintilla-qt.cmake`（Qt 构建脚本）
- 许可证文件：`LICENSE.txt`（HPND）

### 构建策略

**方式 A（推荐）：直接使用 `scintilla-qt.cmake`**

```cmake
FetchContent_Declare(scintilla
    GIT_REPOSITORY https://github.com/diegoiast/scintilla-code.git
    GIT_BRANCH cmake-support
)
FetchContent_MakeAvailable(scintilla)
include("${scintilla_SOURCE_DIR}/scintilla-qt.cmake")
```

**方式 B（备选）：手动收集源文件**

如果 `scintilla-qt.cmake` 有问题，手动从 `qt/ScintillaEditBase/` 和 `src/` 收集源文件。

### 原型结构

```
examples/scintilla_prototype/
├── main.cpp              # Qt 应用入口，创建 ScintillaEditBase 窗口
├── CMakeLists.txt        # 独立 CMake 构建（或通过主 CMakeLists.txt 可选编译）
└── README.md             # 使用说明（可选，仅当用户要求时创建）
```

### ScintillaEditBase API 使用

核心操作：
```cpp
// 设置文本
edit->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(text.c_str()));

// 获取文本长度
auto len = edit->send(SCI_GETTEXTLENGTH);

// 获取文本
std::string text(len + 1, '\0');
edit->send(SCI_GETTEXT, len + 1, reinterpret_cast<sptr_t>(text.data()));

// 撤销
edit->send(SCI_UNDO);

// 选择全部
edit->send(SCI_SELECTALL);

// 插入文本
edit->send(SCI_INSERTTEXT, pos, reinterpret_cast<sptr_t>(text.c_str()));

// 连接信号
connect(edit, &ScintillaEditBase::savePointChanged, ...);
connect(edit, &ScintillaEditBase::modified, ...);
```

### 验证测试

原型需要演示：
1. **基本编辑**：输入文字、删除、撤销/重做
2. **选择操作**：选中文字、复制、剪切
3. **滚动操作**：垂直/水平滚动
4. **IME 事件**：中文输入法事件接收（在代码中打印日志）
5. **信号验证**：打印 `savePointChanged`、`modified` 等信号触发

### 许可证记录

需要在文档中记录：
- Scintilla HPND 许可证全文（或路径）
- 与 QScintilla GPL-3.0 的对比
- 对项目商业使用的合规性结论

## 风险与缓解

| 风险 | 缓解措施 |
|------|---------|
| `scintilla-qt.cmake` 可能与 Qt6 不完全兼容 | 准备手动源文件收集备选方案 |
| 跨平台构建问题（Windows/CI） | 先在 macOS 验证，记录 Windows 已知问题 |
| FetchContent 网络问题 | 检查 GitHub 可达性，必要时使用本地缓存 |

## 不做什么

- 不集成到 `markdown_qt_gui` 主目标
- 不添加 Scintilla 到核心库（保持 core 无 Qt 依赖）
- 不实现文档后端适配层（M09 工作）
