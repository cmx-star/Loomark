# 实施计划：GUI 大文件窗口化接入

## 切片顺序（每片独立可验证）

1. **切片 A — core 磁盘空间查询**
   - 写入 `src/core/document_file.{h,cpp}`：`availableDiskBytes()`（statvfs / GetDiskFreeSpaceExW）
   - `tests/core_tests.cpp` 补用例
   - 验证：构建 + core_tests 通过

2. **切片 B — 分档打开 + LARGE/EXTREME UI 模式**
   - `main_window.{h,cpp}`：loadDocument 分支确认框、`largeMode_`、折行关闭
   - 验证：构建通过；生成 >64MB 样本手动打开确认提示与状态栏档位

3. **切片 C — 窗口化加载与保存拼接**
   - main_window：windowStart_/windowEnd_、初始窗口、窗口导航菜单、保存三段拼接、meta 展示
   - 验证：构建通过；对 100MB 样本开窗、编辑窗口内文本、保存后 diff 校验头部尾部未动、中间生效；内存不随全文增长（活动监视器抽查）

4. **切片 D — 后台预览索引**
   - 新增 `src/gui/preview_index_worker.{h,cpp}`；CMakeLists 注册
   - refreshPreview 分流：Normal 走旧路径，Large/Extreme 走后台索引 + generation 防串台
   - 验证：构建通过；大样本预览显示块内容与截断标注；UI 可交互

## 全局验证

- `cmake --build --preset macos-debug` 全绿
- ctest / core_tests 全绿
- GUI 手动场景清单（PRD AC 1–8）逐项核对，无法自动化项如实记录

## 风险与注意

- UTF-8 边界回退必须只回退 ≤3 字节且落在窗口读取结果内部
- 窗口切换前必须处理 dirty_，防丢改动
- 后台线程禁止触碰 widget；结果经 QFutureWatcher 回 UI 线程
- 保存拼接时 Normal 与 Large 路径 BOM 处理不同，需分别验证
