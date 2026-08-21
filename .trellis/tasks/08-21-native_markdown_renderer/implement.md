# 原生 Markdown AST 渲染器实施计划

## 允许修改

- `CMakeLists.txt`：固定获取并链接 `md4qt`，注册渲染器测试。
- `src/gui/markdown_document_renderer.h`
- `src/gui/markdown_document_renderer.cpp`
- `src/gui/main_window.h`
- `src/gui/main_window.cpp`
- `tests/markdown_document_renderer_tests.cpp`
- `README.md`
- 当前 Task 的需求、设计、计划和进度文件。

发现必须新增其他产品文件或改变公共核心接口时，先重新说明范围并取得批准。

## 不变量

- 不引入 WebEngine、JavaScript、Scintilla/QScintilla 或公式渲染依赖。
- 不修改文件格式、原子保存语义和 CLI 行为。
- 不回退现有未提交 GUI、主题、CMake preset 和 Trellis 改动。
- 不执行提交、分支切换、推送或发布。

## 实施顺序

1. 在 CMake 中配置固定提交的 `md4qt`，关闭无关上游目标并验证可配置。
2. 实现 AST 到 `QTextDocument` 的原生块级与行内渲染。
3. 将 `MainWindow::refreshPreview()` 切换到新渲染器，保持窗口化和滚动行为。
4. 增加渲染器测试，覆盖常用语法、安全降级和结构输出。
5. 更新 README，记录架构、依赖、首次联网要求以及 Mermaid 后续项。
6. 执行构建、测试、GUI 启动与针对性代码审查。

## 验证命令

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
open ./build/macos-debug/markdown_qt_gui.app
```

GUI 可见检查使用本地代表性 Markdown fixture，确认标题、样式、列表、代码、引用、表格、链接以及大文件截取提示均可见。
