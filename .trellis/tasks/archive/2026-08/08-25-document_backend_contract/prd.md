# IDocumentBackend 契约定义

## Goal

定义 `mqt::core::IDocumentBackend` 抽象契约：以「文档快照 + 版本号」为中心，统一读取、定位、搜索、修改应用与安全保存语义。让上层（未来的 DocumentSession、Scintilla 后端、预览索引、AI）只依赖该接口，不再各自直接读 UI 文本或磁盘文件。

本任务交付**契约 + 一个基于现有文件 API 的参考实现（FileDocumentBackend）+ 契约测试**。不接入 GUI，不引入新依赖。

对齐路线图批次 2 模块 M05：core 契约测试覆盖版本变化、范围错误和取消。

## Background

当前 core 能力以「路径 + 函数」形式散落：

- `statFile` / `inspectFile`：元数据与换行分类
- `readRange(path, ByteRange)`：窗口化字节读取
- `searchLiteral` / `locateByteRange`：搜索与行列定位
- `AtomicFileWriter` / `writeFileAtomically`：原子保存
- `buildPreviewIndex`：块索引

问题：GUI 的 main_window 直接组合这些函数并自持 `windowStart_/windowEnd_`、`documentGeneration_`、`previewGeneration_` 等版本计数；后续 Scintilla 后端、Diff、AI 都需要同一套「文档版本 + 字节范围」事实源。若现在不定契约，各模块会各自解析正文、各自造版本号。

## Requirements

### R1 契约核心概念

- `IDocumentBackend` 抽象类（纯虚接口），围绕以下概念定义：
  - **Snapshot**：不可变文档视图（大小、版本号、BOM/换行元数据），一次获取后用于一致性判断。
  - **版本号（version）**：单调递增 uint64；任何成功的内容变更（apply/save/reload）使旧 snapshot 失效。
  - **取消（cancelFlag）**：所有可能耗时的操作接受 `const std::atomic_bool*`，取消后返回带 `cancelled` 标记的结果或抛出约定异常，不得返回部分数据冒充完整结果。

### R2 契约操作集

- 元信息：`info()` 返回档位、大小、BOM、换行风格。
- 读取：`snapshot()` 与 `read(range, version)`；range 越界必须抛错而非截断。
- 定位：`locateLines(range)` 把字节范围映射为起止行列（复用 locateByteRange 语义）。
- 搜索：`search(needle, options)` 返回命中与 truncated 标记（复用 searchLiteral 语义）。
- 应用编辑：`apply(edits, baseVersion)` —— baseVersion 不匹配当前版本时拒绝（StaleVersion 错误），不产生部分写入。
- 安全保存：`save()` 走 AtomicFileWriter 流式路径；`saveAs(path)` 另存。
- 重载：`reload()` 从磁盘重建状态并递增版本。

### R3 参考实现

- `FileDocumentBackend`：包装现有 document_file/markdown_index 函数，持有打开的 FileInfo；作为契约的第一个实现，也用于验证契约可用性。
- 编辑缓冲语义与现有 GUI 窗口化行为一致：apply 只作用于已声明的字节范围，头部尾部原样保留。

### R4 契约测试

- 版本变化：apply 成功后 version 递增；stale baseVersion 被拒且不改动内容。
- 范围错误：越界 read/locate 抛出异常，原状态不变。
- 取消：耗时操作在 cancelFlag 置位后提前返回，结果标记 cancelled。
- 保存失败路径：目标不可写时不破坏原文件（复用现有 AtomicFileWriter 测试思路）。

## Constraints

- C++20，仅标准库；不引入 Qt 依赖进 core（保持现有 core 无 Qt 状态）。
- 不修改 GUI 行为；main_window 本任务不动（接线属后续任务）。
- 新增文件仅限 `src/core/document_backend.{h,cpp}`（或等价拆分）与 `tests/document_backend_tests.cpp`，以及 CMakeLists 注册。
- 不引入 Qt/QML/Scintilla/QScintilla/MD4C/SQLite 等依赖。

## Out of Scope

- M06 Scintilla 集成原型
- M07 对照验证、M08 决策记录
- M13 DocumentSession / 标签页
- GUI 接线、全文搜索替换优化、增量撤销栈

## Acceptance Criteria

- [ ] AC1 契约头文件定义 snapshot/read/locateLines/search/apply/save/reload 与版本、取消语义，core 编译无 Qt 依赖。
- [ ] AC2 FileDocumentBackend 通过契约测试套件：版本变化、stale apply 拒绝、范围错误抛异常、取消提前返回、保存失败保留原文件。
- [ ] AC3 apply 使用过期 baseVersion 时返回明确错误且文档内容与版本均不变。
- [ ] AC4 所有可耗时操作支持 cancelFlag；置位后返回 cancelled 结果且不产出部分数据。
- [ ] AC5 `cmake --build --preset macos-debug` 全绿；新增 `markdown_qt_document_backend_tests` ctest 通过；既有 5 个 ctest 不回归。

## Notes

- 契约设计须保证未来 Scintilla 后端可实现：接口不暴露 ifstream/路径细节，读写都经版本化句柄。
