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


## Session 3: D01 冻结 + 批次 3 启动，M09 完成

**Date**: 2026-08-29
**Task**: 08-28-m09_scintilla_backend
**Branch**: `feat/large-file-windowed-gui`

### Summary

D01 决策按用户「推进项目整体到整体完成」指令以默认批准生效冻结（可否决回退）。M09 ScintillaDocumentBackend 全部验收通过，批次 3 首个模块落地。

### Main Changes

- `src/backend/scintilla_document_backend.{h,cpp}`：IDocumentBackend 全实现，Scintilla 为唯一事实源
- GUI：QStackedWidget 双编辑器（NORMAL=Scintilla 后端，windowed=旧路径回退）；保存 8MiB 分块原子写
- 修复隐藏编辑器 wrap 重排版卡死；契约测试 24 用例 + GUI 字节保真/编辑持久化用例

### Testing

- [OK] 构建 0 错误 0 警告（上游 1 处除外）；ctest 8/8；diff-check 通过；offscreen GUI 冒烟正常

### Status

[OK] **M09 完成。下一步 M10：分块 Loader 与稀疏行索引**


## Session 4: M10 分块 Loader 与稀疏行索引完成

**Date**: 2026-08-29
**Task**: 08-29-m10_chunked_loader_line_index
**Branch**: `feat/large-file-windowed-gui`

### Summary

后台分块装载落地：core 扫描器（稀疏行索引/流式指纹/换行统计）+ ScintillaLoadTask + GUI 集成，300MB 验收双通过。

### Testing

- [OK] ctest 9/9；300MB 装载 845ms、UI 最大间隔 61ms；取消 12ms 清空；diff-check 通过

### Status

[OK] **M10 完成。下一步 M11：大文件搜索替换**


## Session 5: M11 分批搜索替换完成

**Date**: 2026-08-29
**Task**: 08-29-m11_batched_search_replace
**Branch**: `feat/large-file-windowed-gui`

### Summary

searchBatch 分批搜索（上限/超时/取消）+ applyReplace 32MiB 确认门 + GUI 查找/替换栏。300MB 搜索 6 批 39960 命中 5ms，取消立即返回。

### Testing

- [OK] 契约测试新增 6 用例；ctest 9/9；clean 构建 0 警告；diff-check 通过

### Status

[OK] **M11 完成。下一步 M12：流式保存与撤销预算 → F02**


## Session 6: M12 完成并通过 F02 功能门

**Date**: 2026-08-29
**Task**: 08-29-m12_streaming_save_undo_budget
**Branch**: `feat/large-file-windowed-gui`

### Summary

恢复点（版本保存点）、外部指纹复核（保存前流式比对磁盘基线）、撤销内存预算（64MiB 默认，超额清空）落地。F02「300MB 桌面完整编辑」端到端验收通过：装载→编辑→搜索→替换确认门→流式原子保存→重载复核。

### Testing

- [OK] 契约测试新增 4 用例（36 总数）；ctest 9/9；F02 VERIFY PASS

### Status

[OK] **批次 3 完成（F02 达成）。下一步批次 4：M13 DocumentSession**
