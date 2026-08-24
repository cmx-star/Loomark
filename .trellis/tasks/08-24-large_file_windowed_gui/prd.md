# GUI 大文件窗口化接入

## Goal

把 core 层已有的大文件能力接入 Qt Widgets GUI：打开时按档位（file_tier）分级提示与行为，预览改走 buildPreviewIndex 流式块索引，编辑器侧大文件路径用 readRange 窗口化读取，并强化安全保存。让用户能在 GUI 中打开数百 MB 的 Markdown 文件而不卡死或内存膨胀。

对齐总规划 `large-markdown-editor-technical-solution-and-roadmap.md` §8（§8.1 档位矩阵、§8.3 加载、§8.4 高成本操作保护）与 AGENTS.md P0 目标（large-file open, byte-range reading, safe save paths, streaming preview indexing）。

## Background

core API 已就绪但 GUI 未接入，存在明显断层：

- `src/gui/main_window.cpp` 打开文件时仅用 `inspectFile` 拒绝 Reject 档，随后一律 `readRange(path, {0, sizeBytes})` 全文读入内存。
- 预览文本由 `collectPreviewText()` 生成，固定 256KiB 全文 / 128KiB 窗口化截断，未按 file_tier 分档，也未使用 `buildPreviewIndex` 流式索引。
- core 侧已具备：`inspectFile()`（大小/BOM/换行/FileTier）、`readRange(path, ByteRange)`、`buildPreviewIndex(path, BuildPreviewOptions)`（Heading/Paragraph/CodeFence 块索引，带 truncated 上限）、`classifyDesktopFile()`（NORMAL ≤64MB / LARGE 64~256MB / EXTREME 256~512MB / REJECT >512MB）。

本任务是 GUI 接入而非重写 core。

## Requirements

- 打开文件时按 `inspectFile` 结果分档处理：
  - NORMAL：维持现有完整编辑体验。
  - LARGE：提示用户进入大文件模式；关闭默认折行等高成本行为（§8.1/§8.4）。
  - EXTREME：明确提示性能受限；无全文样式、限制高成本操作（§8.1）。
  - REJECT：拒绝打开并说明原因。
- 预览改走 `buildPreviewIndex` 流式索引替代固定字符截断；预览内容来自块索引（标题/段落/代码围栏），尊重 maxBlocks/truncated 上限，truncated 时向用户展示截断状态。
- 编辑器侧大文件路径接入窗口化读取：LARGE/EXTREME 档不再一次性 `readRange` 全文，按可视范围/窗口分块读取（§8.3 加载）。
- 后台索引、预览构建等耗时工作不得阻塞主线程；后台代码不直接触碰 UI 对象（UI 与后台分离）。
- 安全保存强化：保存前检查目标磁盘可用空间，磁盘不足时不写入、保留原文件完好并给出错误提示（§8.4 高成本保护基线）。
- 主编辑、主预览不依赖 WebView；不引入 Qt/QML/Scintilla/WebEngine 新依赖。
- 行为保持跨平台，不绑定 macOS 专属 API。

## Acceptance Criteria

- [x] 打开 ≤64MB 文件，行为与现状一致（完整编辑、预览正常），无新增降级提示。
  - 证据：Normal 档路径保持原有全文读入与实时预览逻辑；离屏探针小文件打开正常。
- [x] 打开一个 64~256MB 文件，出现大文件模式提示，且应用保持可响应（不因全文读入而长时间无响应）。
  - 证据：探针捕获 LARGE 档确认框文案；窗口化加载（仅 2MiB）后可立即触发菜单操作。
- [x] 打开一个 256~512MB 文件，出现 EXTREME 档提示，编辑器进入受限模式且主界面不冻结。
  - 证据：探针捕获「将以受限模式打开」确认框；NoWrap + 窗口化生效，全程可交互。
- [x] 尝试打开超过 512MB 的文件被拒绝并显示原因。
  - 证据：探针对 520MB 样本捕获拒绝对话框「文件超过 512 MiB 上限，当前版本暂不支持打开。」。
- [x] 大文件打开后预览区基于 buildPreviewIndex 块索引渲染标题/段落/代码围栏内容，达到上限时明确显示截断状态。
  - 证据：预览元信息显示「索引预览（已保存内容）· 块 800 · 扫描 81.1 KiB / 共 300.0 MiB · 已截断」。
- [x] LARGE/EXTREME 档下编辑器侧内存占用不随文件大小线性增长到全文件规模（窗口化读取生效）。
  - 证据：探针断言编辑器始终仅持一个 ~2MiB 窗口（300MB 文件下 characterCount≈2MiB）；活动监视器级复核由用户手动完成。
- [x] 耗时索引/预览构建期间 UI 不冻结（可交互或可取消）。
  - 证据：索引在 PreviewIndexThread 后台线程执行，主线程持续处理事件（探针在索引期间连续驱动交互无阻塞）。
- [x] 模拟磁盘空间不足时保存失败，原文件内容未被破坏，且有明确错误提示。
  - 证据：探针用 8MB dmg 填满后保存，弹窗「磁盘可用空间不足，无法安全保存：本次约需 1 MiB，当前仅剩 0 MiB。」，原文件字节级完好。

## Out of Scope

- Scintilla/QScintilla 或 QML 编辑器替换
- 全文搜索替换的大文件优化（另行任务）
- 目录/大纲面板、Diff、RAG 与 AI 能力
- 移动端档位（§8.2）
- 无效 UTF-8 只读诊断模式
- 标签页持久化与阅读位置恢复
- 增量编辑回写（本任务只保证安全保存既有编辑缓冲的路径）

## Notes

- 实施开始前先用 trellis-before-dev 加载 `.trellis/spec/` 相关规范。
- core 单元测试已覆盖各 API；GUI 接入层验证以手动 + 可自动化部分为准。
