# PRD: M06 Scintilla 直接集成原型

## 状态

- 批次：批次 2
- 优先级：P0（架构关键验证）
- 状态：进行中

## 背景

M05 已完成 `IDocumentBackend` 契约定义，为后续正式后端决策提供了统一接口。下一步需要验证 Scintilla 作为正式桌面编辑后端的可行性。

当前 GUI 使用 `QPlainTextEdit`，该组件对大文件（>100MB）有严重性能问题。原方案推荐使用 Scintilla，但需要单独验证其 Qt 集成可行性和许可证合规性。

**项目规则约束：**
- QScintilla 默认不采用；许可证决定未解决前不得作为便捷替代（QScintilla 使用 GPL-3.0）
- Scintilla 本体使用 HPND（Scientific Toolworks 自由许可证），与 Python/Perl 类似，属于宽松许可证，允许商业使用

## 目标

在隔离原型中验证以下能力，**不改动现有 GUI 代码**：

1. ScintillaEditBase 通过 FetchContent 集成到 Qt6 项目
2. Qt 事件处理（键盘、鼠标、滚轮）
3. 输入法（IME）事件接收
4. 文本选择和撤销操作
5. 跨平台构建（macOS 实测，Windows/Linux 至少 CMake configure 成功）
6. 记录许可证声明和包体大小

## 范围

**包含：**
- `examples/scintilla_prototype/` 下的独立可运行程序
- `CMakeLists.txt` 中的 FetchContent Scintilla 配置
- 许可证信息记录
- 构建验证脚本/记录

**不包含：**
- 不修改 `src/gui/main_window.*`
- 不修改 `src/core/` 任何代码
- 不接入 `IDocumentBackend`（那是 M09 的工作）
- 不实现语法高亮（验证阶段不需要）

## 验收条件

- [ ] 原型可编译并通过 `clang++`/`g++`/`msvc` 至少一种编译器构建
- [ ] macOS 上可运行，显示编辑界面
- [ ] 基本编辑操作（输入文字、选中、撤销 Ctrl+Z）在工作
- [ ] 输入法事件能被接收（inputMethodEvent 被调用）
- [ ] `scintilla-qt.cmake` 中所有源文件被正确收集
- [ ] 许可证文件（License.txt）路径已记录
- [ ] 不引入任何 QScintilla 依赖

## 非目标

- 不实现大文件专用优化（这是 M09-M12 的工作）
- 不实现与主 GUI 的集成
- 不做性能基准测试（这是 M07 的工作）
