# M09 正式桌面后端接入

## Goal

批次3 M09：实现 `ScintillaDocumentBackend` 实现 `IDocumentBackend` 契约，接入 GUI，正文只保留一个事实源（Scintilla 字节缓冲），消除 UI 第二份完整字符串副本。

**启动前置条件：D01 决策获批（`docs/m08-backend-decision-draft.md`）。在获批前本任务保持 planning，不进入实施。**

## Requirements

- `ScintillaDocumentBackend` 实现 M05 契约（`src/core/document_backend.h`）：snapshot、read、locateLines、apply、search、版本与取消语义
- 后端持有 Scintilla 文档实例作为正文事实源；版本号 + 指纹构成 snapshot 身份
- GUI 主窗口 NORMAL 档切换到后端路径完成打开、编辑、保存往返
- 位置语义对齐：`IDocumentBackend` 以 UTF-8 字节偏移为准，与 Scintilla 缓冲一一对应；与 GUI（UTF-16 QString）交界处显式转换并注明成本
- QPlainTextEdit 旧路径保留为回退，不删除

## Out of Scope

- 分块后台加载与稀疏行索引（M10）
- 大文件搜索替换策略、正则超时（M11）
- 流式保存、撤销内存预算、外部指纹复核（M12）
- 语法高亮、主题、300MB 完整编辑闭环（F02 在 M10–M12 汇合后验收）

## Acceptance Criteria

- [ ] `ScintillaDocumentBackend` 通过 `IDocumentBackend` 契约测试（复用/扩展 `tests/document_backend_tests.cpp` 语义：版本变化、范围错误、取消）
- [ ] 编辑、撤销、选择、保存不经过第二份完整 UI 字符串（代码审查 + 内存上限断言）
- [ ] GUI NORMAL 档经后端路径的打开→编辑→保存→重载往返与现路径行为一致
- [ ] 全量 ctest 通过，无回归
- [ ] `git diff --check` 通过

## Notes

- 复杂任务：实施前补 `design.md`（后端与 ScintillaEditBase 的所有权与线程模型）与 `implement.md`（切片），再 `task.py start`。
- 性能复核沿用 M07 方法（`docs/m07-backend-benchmark.md`），在 F02 前复测。
