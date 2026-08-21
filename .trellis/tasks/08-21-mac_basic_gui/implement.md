# 跨平台基础GUI执行计划

## 顺序

1. 调整构建系统，让 Qt Widgets GUI 能和现有核心库一起编译。
2. 新增桌面 GUI 入口和主窗口。
3. 接入打开、保存、另存为、脏状态和预览刷新。
4. 让状态栏和标题栏显示当前文件状态。
5. 补充核心测试和最小构建验证。
6. 更新 README，让人知道怎么跑 GUI。

## 预计改动文件

- `CMakeLists.txt`
- `CMakePresets.json`
- `src/gui/main.cpp`
- `src/gui/main_window.h`
- `src/gui/main_window.cpp`
- `src/core/document_file.cpp`
- `src/core/document_file.h`
- `tests/core_tests.cpp`
- `README.md`

## 验证命令

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
./build/macos-debug/markdown_qt_app
```

## 风险点

- `Qt Widgets` 需要正确发现 `qtbase`。
- 首版预览可能只覆盖常见 Markdown 子集。
- 保存逻辑必须继续使用原子写入，不能回退到直接覆盖文件。

## 回退点

- 如果 GUI 构建失败，先保留核心库和测试，通过 CMake 选项或目标拆分回退到仅核心构建。
- 如果预览刷新太慢，先保留基础刷新，不在这一轮里引入更复杂的增量机制。
