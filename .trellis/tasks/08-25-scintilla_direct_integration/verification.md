# M06 验证记录：Scintilla 直接集成原型

> 验证日期：2026-08-28
> 平台：macOS 15 (darwin 24.6.0, arm64)，AppleClang，Qt 6 (Homebrew)，CMake 4.4.2
> Scintilla：5.3.3（diegoiast/scintilla-code `cmake-support` 分支，解析提交 `2812df4eb4b95bcebe4cbba656cd024c28415d20`）

## 构建证据

- `markdown_qt_scintilla`（ScintillaEditBase + 引擎全量源）静态库编译通过，零警告。
- `markdown_qt_scintilla_prototype`（隔离 GUI 原型）编译通过，零警告。
- 全量 `ctest`：6/6 通过（core、document_backend、window_boundary、renderer、update_checker、main_window），无回归。
- Qt6 兼容垫片 `cmake/qt_textcodec_compat.h` 本次补齐 `canEncode()`（CaseFolderDBCS 依赖）；所有编码仍按 UTF-8 简化，符合原型范围。

## PRD 验收条件核对

| 验收条件 | 结果 | 证据 |
| --- | --- | --- |
| 至少一种编译器构建 | ✅ | AppleClang，见上 |
| macOS 上可运行，显示编辑界面 | ✅ | 原型进程启动，创建 900×628 主窗口「M06 Scintilla Direct Integration Prototype (HPND)」，事件循环稳定运行（无崩溃日志） |
| 基本编辑（输入、选中、撤销 Ctrl+Z） | ✅ | 自动化冒烟测试 10/10（详见下） |
| 输入法事件可接收（inputMethodEvent 被调用） | ✅ | 冒烟测试 `inputMethodEvent override invoked` + `IME commit string lands in buffer`（中文提交串「中文」正确落入缓冲） |
| `scintilla-qt.cmake` 源文件正确收集 | ✅（替代方案） | 采用 design.md 的方式 B：主 CMakeLists 显式收集 ScintillaEditBase + src/ 全量源（fork 自带脚本依赖 Qt6Core5Compat，不符合项目无新增依赖约束）。全部目标编译链接通过即证明收集完整 |
| 许可证文件路径已记录 | ✅ | `build/_deps/scintilla-src/License.txt`（HPND，"License for Lexilla, Scintilla, and SciTE"，Copyright 1998-2021 Neil Hodgson）。源码树体积 55MB（FetchContent 后） |
| 不引入 QScintilla | ✅ | 依赖仅 diegoiast/scintilla-code 源码；未链接任何 QScintilla 组件 |

## 自动化冒烟测试（QT_QPA_PLATFORM=offscreen）

`examples/scintilla_prototype/prototype_smoke.cpp`，`markdown_qt_scintilla_smoke` CTest 目标：

```
[PASS] SCI_SETTEXT/SCI_GETTEXT roundtrip
[PASS] keyboard typing inserts text
[PASS] Ctrl+Z undoes typed text
[PASS] Ctrl+Y redoes typed text
[PASS] SCI_SELECTALL selects whole buffer
[PASS] inputMethodEvent override invoked
[PASS] IME commit string lands in buffer
[PASS] modified signal emitted on insert
[PASS] savePointChanged signal emitted
[PASS] undo removes programmatic insert
SMOKE RESULT: ALL PASS
```

## 交互验证边界（残余风险）

- 本验证环境无显示器捕获能力，鼠标拖拽选择、滚轮滚动未做人工 GUI 实测；
  选择与滚动逻辑分别由 SCI_SELECTALL（自动化覆盖）和 Scintilla 上游成熟度背书。
- 中文输入法端到端（真实 IME 候选窗）未实测；已验证 QInputMethodEvent 提交路径。
- Windows/Linux 交叉构建未验证（PRD 允许“至少 configure 成功”为后续 CI 项，记录为残余项）。

## 结论

M06 验收条件除跨平台 CI 项外全部满足，Scintilla + Qt6 直接集成本地构建与自动化行为验证通过，可进入 M07 对照基准。
