# 设计: M10 分块 Loader 与稀疏行索引

## 已确认事实

- Scintilla 消息只能在 UI 线程执行 → 后台线程只做文件读取/扫描，追加必须回到 UI 线程（queued signal）。
- M07 基准：300MB 原始读取 ~70ms；SCI_SETTEXT 一次性灌入 300MB ~230ms。分块追加单块毫秒级。
- 现有后台线程先例：`PreviewIndexThread`（QThread 子类 + cancel 标志 + generation 防串台）。
- M09 后端已固化：BOM 剥离/回写、UTF-8 字节偏移语义、版本过滤信号。

## 架构

```
ScintillaLoadTask (QThread, worker)
  run(): 循环读 4MiB 块
    ├─ FingerprintSink::update(chunk)        （core，纯计算）
    ├─ LineIndexBuilder::feed(chunk)         （core，纯计算：逐行记偏移）
    ├─ NewlineCounter::feed(chunk)           （core）
    ├─ emit chunkReady(QByteArray)           （queued → UI 线程 SCI_APPENDTEXT）
    └─ cancelFlag 检查 → 提前退出 emit cancelled()
UI 线程（ScintillaDocumentBackend）
  onChunkReady → SCI_APPENDTEXT（毫秒级）
  onFinished  → info_ 落定（sizeBytes/newline/fingerprint）、版本保持、装载结束信号
```

### D1 稀疏行索引（core/line_index.h，无 Qt）

- `LineIndexBuilder::feed(std::string_view chunk)`：扫描 chunk 内 `\n`（CR 裸行由 NewlineCounter 统计，行切分以 `\n` 为准 + 末尾 CR 特判），每见行首调 `record(line, offset)`。
- `SparseLineIndex`：`std::vector<uint64_t> offsets_` 只存**每 kSparseEvery=256 行**的行首偏移（第 0 行必存），`lineCount` 单独累计。
- 查询：
  - `offsetOfLine(line)`：line % 256 == 0 直接得；否则返回 ≤line 的最近锚点 + 距离（调用方自行精扫，M11 用）。
  - `lineOfOffset(offset)`：锚点数组二分，返回 ≤offset 的最近锚点行（精确行需文本扫描，M11 组合）。
- 装载完成时 offset = 文档长度也记录为哨兵（末行定位）。

### D2 流式指纹（core/document_file.h 增补）

- `class FingerprintSink { void update(std::string_view); uint64_t value() const; }`：FNV-1a 64 位。
- 空内容指纹 = FNV 偏移基值（确定性）。M12 保存后重算对比。

### D3 换行统计

- `struct NewlineCounts { lf, crlf, cr; }` + `feed()`：逐字节状态机，跨块保存 lastWasCR 状态。
- 归类规则（对齐现有 normalizeLineEndings 语义）：crlf ≥ lf 且 crlf ≥ cr → CRLF；否则 lf ≥ cr → LF；否则 CR；全 0 → None/unknown。

### D4 ScintillaLoadTask（backend/scintilla_load_task.h）

- QThread 子类，构造参数：path、块大小（默认 4MiB）、cancel 标志指针。
- 信号：`chunkReady(QByteArray bytes)`（queued）、`progress(uint64 loaded, uint64 total)`、`finished(bool ok, QString error)`。
- run()：打开文件（含 BOM 头块特判：首块剥离 BOM 后再发）→ 循环读/算/发 → 结束发 finished。
- 取消：cancelFlag 置位后停止读取；已发 chunk 由 UI 侧 SCI_CLEARALL 丢弃；析构函数 quit+wait（资源确定释放）。
- UI 侧所有权：backend 持有 task 指针；loadDocument 重入或 cancel 后 delete（QThread wait 保证）。

### D5 backend 集成

- `ScintillaDocumentBackend::loadInBackground()`：清空缓冲 → 启动 task；`onChunkReady` 内 `mutatingDocument_ = true` + `SCI_APPENDTEXT`；`onFinished(ok)`：成功 → 由 task 提取的 LineIndex/fingerprint/换行落到成员，`info_` 落定、`SCI_EMPTYUNDOBUFFER`；失败/取消 → `SCI_CLEARALL` + 释放 task。
- 新增访问器：`lineIndex()`、`fingerprint()`。
- 同步路径（小文件 ≤ 1MiB）保留现构造函数直接装载，避免测试与简单场景引入线程。

### D6 GUI 集成

- loadDocument：sizeBytes > 1MiB 的 Normal 档走后台装载；期间 `saveAction_` 禁用、状态栏「装载中 x%」、重复打开先 cancel 旧任务。
- Large/Extreme 档：本模块只打通装载能力（确认对话框改为走后台装载提示文本延后到 M12 一并切）；windowed 路径本轮不动，避免 M10 范围失控。
  （修正：路线图 M10 验收「300MB 打开不冻结」由后台装载能力 + 验收测试证明；GUI 端到端切换在 M12 F02 收口。）

### D7 线程与生命周期

- worker 内不做任何 Qt UI 调用；信号跨线程自动 queued。
- task 析构：`quit()+wait()`；backend 在 finished 处理后 delete task（`deleteLater`）。
- 测试（offscreen）：装载期间循环 processEvents 计数验证事件循环活跃；cancel 后 `isRunning()==false` 断言。

## 风险

| 风险 | 缓解 |
| --- | --- |
| 4MiB 块在 UTF-8 多字节中间切开 | 行索引按块边界续扫（builder 跨块状态机）；Scintilla 追加按字节流，多字节切块无碍（首尾拼接即可，无对齐要求） |
| queued 信号积压导致内存峰值（300MB 全部排队） | worker 每 emit 后等待 UI 消费：用信号量式节流（未确认块数 > 2 时 worker 短暂让渡），保证峰值 ≤ 块大小×常数 |
| 末行无换行符 | builder 行首记录覆盖；newLine 哨兵处理 |
