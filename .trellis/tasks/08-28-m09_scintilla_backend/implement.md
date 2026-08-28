# 实施: M09 ScintillaDocumentBackend

> 前置：D01 获批后方可 `task.py start` 并执行下列切片。

## 切片

### 切片 A — 后端主体与契约测试（先行，不触碰 GUI）

- 新增 `src/backend/scintilla_document_backend.{h,cpp}`（CMake 目标 `markdown_qt_backend`，链接 core + scintilla + Qt6::Widgets）
- 实现 `IDocumentBackend` 全部方法（设计 D2–D6）
- 新增 `tests/scintilla_document_backend_tests.cpp`（设计 D8），offscreen 运行
- 验收：契约测试全绿；现有 7 个 ctest 无回归

### 切片 B — GUI NORMAL 档切换后端路径

- `src/gui/main_window.{h,cpp}`：构造后端（注入可见 ScintillaEditBase——注：M09 阶段主编辑器仍是 QPlainTextEdit，后端包装的 ScintillaEditBase 在本切片中先以隐藏实例接入 **仅 NORMAL 档打开/编辑/保存往返的数据面**；可见编辑器切换在切片 C）

  > 修正：若审查发现隐藏实例构成第二事实源（正文同时进 QPlainTextEdit），则本切片直接跳到切片 C 的可见切换，不得保留双实例。
- 保存/另存为/重载走后端路径；状态栏显示版本号
- 验收：NORMAL 档打开→编辑→保存→重载往返一致； LARGE/EXTREME 档行为不变（仍走窗口化桥接）

### 切片 C — 可见编辑器切换与回退保留

- 主编辑器替换为后端包装的可见 ScintillaEditBase（NORMAL 档）；QPlainTextEdit 路径整类保留（回退开关 `MQT_LEGACY_EDITOR` 编译宏 + 运行时菜单切换）
- 人工清单：输入、选择、撤销/重做、IME、保存往返、回退开关
- 验收：PRD 验收条件全部勾选；`git diff --check`；全量 ctest 绿

## 完成定义

- PRD 验收条件逐项核对并记录 verification.md
- journal 记录会话；提交按切片拆分
