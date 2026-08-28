# M10 分块 Loader 与稀疏行索引

## Goal

批次3 M10：把 LARGE/EXTREME 档的打开路径从「窗口化 QPlainTextEdit」演进到「Scintilla 后台分块装载」：
后台线程分块读取文件、增量灌入 Scintilla 缓冲，同时构建稀疏行索引与流式指纹；全程可取消，
取消后资源确定释放。为 M11（后台搜索替换）与 M12（流式保存/撤销预算）铺平 300MB 完整编辑之路。

## Requirements

- 后台分块装载：worker 线程读文件分块（含 UTF-8 边界安全），UI 线程增量追加进 Scintilla 缓冲，
  UI 线程单次追加耗时为毫秒级（不冻结）
- 稀疏行索引：装载过程中按行扫描记录行首偏移，稀疏存储（每 N 行记录一次），
  支持 offset→最近记录行 与 line→offset 的查询（供 M11 后台搜索定位）
- 流式指纹：装载时增量计算 64 位内容指纹，供 M12 外部修改复核
- 换行样式检测：装载时统计 LF/CRLF/CR，得出 newlineStyle 与 newlineStyleKnown
- BOM：沿用 M09 语义（剥离头部 BOM，元数据记录，保存时回写）
- 可取消进度：装载期间发出进度信号；cancel() 后 worker 尽快退出、部分内容丢弃、线程确定 join
- GUI：超过阈值的 Normal/Large/Extreme 文档走后台装载；状态栏显示进度；装载完成前禁用保存

## Out of Scope

- 后台搜索替换（M11）
- 流式保存、撤销内存预算、外部指纹复核对比逻辑（M12）
- 300MB 全量编辑的端到端验收（F02 在 M12 后）

## Acceptance Criteria

- [ ] 稀疏行索引与指纹的 core 单测通过（记录/查询/边界）
- [ ] 后台装载完成后的缓冲内容与文件字节一致（含 BOM/CRLF 用例）
- [ ] 300MB 样本后台装载：UI 线程保持事件循环响应（装载期间 processEvents 可执行），总时长可接受
- [ ] cancel() 后：worker 线程在有限时间内退出并可 join，缓冲恢复空，无泄漏（重复 open/close 循环稳定）
- [ ] 装载完成后 locateLines/search 语义与同步装载一致
- [ ] 全量 ctest 通过，无回归；git diff --check 通过

## Notes

- 复杂任务：design.md 已随任务起草；实施切片见 implement.md。
