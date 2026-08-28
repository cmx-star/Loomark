# Journal - cmx (Part 1)

> AI development session journal
> Started: 2026-08-21

---



## Session 1: Trellis 任务收尾与工作流修复

**Date**: 2026-08-26
**Task**: Trellis 任务收尾与工作流修复
**Branch**: `feat/large-file-windowed-gui`

### Summary

同步公司工作流并清理已完成但未归档的后端契约 Task。

### Main Changes

- 同步包含 Task 去重、归档与 journal 强制记录规则的 workflow
- 以无提交模式归档 completed 的 document_backend_contract Task

### Git Commits

(No commits - planning session)

### Testing

- [OK] 确认 active Task 仅保留 scintilla_direct_integration
- [OK] 确认 workflow 与公司源哈希一致

### Status

[OK] **Completed**

### Next Steps

- 继续验证 Scintilla 直接集成原型


## Session 2: 批次 2 推进至 G01 决策门（M06 验证 / M07 基准 / M08 草案）

**Date**: 2026-08-28
**Task**: 08-25-scintilla_direct_integration
**Branch**: `feat/large-file-windowed-gui`

### Summary

完成 M06 Scintilla 原型验证，执行 M07 对照基准，起草 M08 后端决策记录，批次 2 全部模块就绪并停在 G01 用户决策门。

### Main Changes

- 补齐 Qt6 QTextCodec 垫片 `canEncode()`；新增 `markdown_qt_scintilla_prototype` 可执行目标（修复 Scintilla 5.x 枚举命名）
- 新增自动化冒烟测试 `markdown_qt_scintilla_smoke`（offscreen，编辑/撤销/选择/IME/信号 10/10 通过）
- 新增分进程对照基准 `markdown_qt_scintilla_bench`：300MB 打开 32.7s→337ms、峰值内存 2.44GB→1.32GB、远端搜索 1.96s→198ms
- 记录 `verification.md`（M06）、`docs/m07-backend-benchmark.md`、`docs/m08-backend-decision-draft.md`

### Testing

- [OK] 全量构建 0 错误 0 警告；ctest 7/7；`git diff --check` 通过
- [OK] 原型 GUI 于 macOS 启动并创建主窗口（无显示器捕获环境，人工视觉项以自动化替代并记录边界）

### Status

[OK] **批次 2 完成，待用户批准 D01（Scintilla HPND 依赖 + 批次 3 迁移范围）**
