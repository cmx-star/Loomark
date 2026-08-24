# 设计：GUI 大文件窗口化接入

## 已验证事实

- `loadDocument()`（main_window.cpp ~L505）：仅拒绝 Reject 档，其余 `readRange(path, {0, sizeBytes})` 全文读入。
- `collectPreviewText()`：固定 256KiB 全文 / 128KiB 视口截断，同步渲染在 UI 线程。
- `writeCurrentDocument()`：editor 全文 → 换行归一 → BOM → `writeFileAtomically`（temp+rename，失败时原文件保留）。
- 预览刷新已有 60ms 单发 QTimer 防抖；`previewTimer_` 触发 `refreshPreview()`。
- CMake 可用：`cmake --preset macos-debug && cmake --build --preset macos-debug`。
- core 测试在 `tests/core_tests.cpp`。

## 方案

### 切片 1：分档打开行为（main_window.cpp）
- `loadDocument()` 按 `info.tier` 分支：
  - Normal：现状不变。
  - Large：确认框说明进入大文件模式（关闭折行、预览按索引、窗口化加载），取消则中止打开。
  - Extreme：更强的受限模式确认框。
  - Reject：保留现有提示，文案写明 >512MB 上限。
- 新增成员 `bool largeMode_`（Large||Extreme 时 true）；`applyTierUiMode()` 中 Large/Extreme 关闭编辑器自动换行（NoWrap + 默认 wrap mode 关闭）。

### 切片 2：窗口化文档缓冲（main_window.cpp/h）
- 仅 Large/Extreme 生效。新增状态：
  - `windowStart_` / `windowEnd_`（字节区间，含头不含尾，相对文件原始字节）
  - `constexpr std::uint64_t kWindowBytes = 2 MiB`
- 打开：初始窗口取 `[bomSkip, bomSkip+kWindowBytes)`，`readRange` 后回退最多 3 字节对齐 UTF-8 边界再截断；Normal 档仍全文。
- 窗口导航（菜单「窗口」）：开头 / 上一窗 / 下一窗 / 跳转指定 MB。切换前若 dirty_ 先走 `maybeSaveChanges()`；新窗口按同规则读取并替换 editor 内容。
- 保存拼接：磁盘原文件头部 `[0, windowStart_)` + 编辑窗口文本（UTF-8→换行归一）+ 尾部 `[windowEnd_, size)`；整体走 `writeFileAtomically`。保存成功后 `windowStart_/windowEnd_` 不变（内容语义不变）。
- BOM：窗口起始跳过 3 字节 BOM；保存时头部原样含 BOM，不再手动插入（与现逻辑差异点：仅 Normal 全文路径保留现有 BOM 插入行为）。
- 状态栏/编辑器 meta 显示「窗口 X–Y / 共 Z」。
- 明确限制：编辑只作用于当前窗口；跨窗口编辑需先保存（记录在 UI 提示与 README 后续说明之外的任务 notes）。

### 切片 3：后台流式预览索引（新文件 preview_index_worker.{h,cpp} + main_window 接线）
- `PreviewIndexWorker`：纯 QObject 工厂式静态方法 + `QtConcurrent::run` + `QFutureWatcher`；输入 path 与 `BuildPreviewOptions`，输出 `indexReady(PreviewIndex)` / `failed(QString)`。后台不触碰任何 widget。
- MainWindow 维护 `previewGeneration_` 原子计数：新请求使旧结果失效，过期结果丢弃。
- 行为：
  - Normal：保持现有编辑器文本实时预览（支持未保存内容）。
  - Large/Extreme：预览改走后台 `buildPreviewIndex(path)`（反映**已保存**内容，meta 标注）；块文本拼接后仍用 `renderMarkdownDocument` 渲染，尊重 maxBlocks/truncated；truncated 时显示截断提示。
- CMakeLists 注册两个新源文件（app target 与 268 行附近的复用列表同步检查）。

### 切片 4：安全保存磁盘预检（core/document_file.{h,cpp} + 调用点）
- 新增 `std::uint64_t availableDiskBytes(const std::filesystem::path& path)`：POSIX `statvfs`，Windows `GetDiskFreeSpaceExW`；失败返回 0 并由调用方按“未知”处理（放行但记录日志？——不放行：返回 max() 表示未知则放行）。定案：查询失败抛异常由上层提示“无法确认磁盘空间”，避免静默写坏盘。
- `writeCurrentDocument()` 写入前校验 `availableDiskBytes ≥ data.size × 2 + 1MiB` 余量（temp 副本瞬时共存）；不足则报错且不触碰目标文件。

## 测试边界

- core：`availableDiskBytes` 对存在目录返回 >0；对不存在路径的行为契约。加入 `tests/core_tests.cpp`。
- 窗口拼接、UTF-8 截断对齐属 GUI 层私有逻辑：以可运行构建 + 手动验证为主；如时间允许把「UTF-8 边界回退」提为 core 小函数以便单测。

## 回退路径

全部改动集中在 gui 两文件 + core 一函数，单 commit revert 即可回退；无数据格式变化。

## 明确不做

- 滚动驱动自动扩窗 / 虚拟化编辑器
- 大文件搜索替换优化、目录面板、Diff、AI
- 无效 UTF-8 只读诊断模式
