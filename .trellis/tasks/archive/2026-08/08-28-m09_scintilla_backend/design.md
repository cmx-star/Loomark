# 设计: M09 ScintillaDocumentBackend 正式桌面后端接入

> 前置：D01 获批（docs/m08-backend-decision.md（已冻结））。本设计为 planning 产物，不含实施代码。

## 已确认事实

- `IDocumentBackend`（M05）以 **UTF-8 字节偏移**为位置语义（`ByteRange`、`TextEdit.start/end`）；Scintilla 在 `SC_CP_UTF8` 下文档位置同样是 UTF-8 字节偏移——两者天然对齐，无需转换层。
- GUI 侧 QString/UTF-16 只存在于显示瞬时转换（绘制、剪贴板），不构成持久第二副本。
- ScintillaEditBase 是 QWidget，必须生存于 UI 线程；`SCI_*` 消息非线程安全。
- core 已提供 `AtomicFileWriter`（流式临时文件 + 原子替换 + 析构清理），save 路径直接复用。
- offscreen 平台下可创建 ScintillaEditBase 并完成全部消息/信号交互（M06 冒烟测试已证）。

## 架构决策

### D1 后端与 widget 的关系：包装可见实例，而非自建隐藏副本

`ScintillaDocumentBackend` **不拥有** ScintillaEditBase；它以非拥有指针包装 GUI 当前显示的编辑器实例：

```
MainWindow ──拥有──> ScintillaEditBase（可见，唯一事实源）
     │                     ▲
     └──拥有──> ScintillaDocumentBackend ──非拥有指针──┘
```

理由：正文只保留一个事实源。若后端自建隐藏 Scintilla 文档，GUI 与后端间就要同步两份缓冲——正是架构红线禁止的形态。

### D2 版本权威：后端是唯一版本计数器

- `version_` 由后端独占维护，初值 `kInitialDocumentVersion`。
- `apply()` 校验 `baseVersion == snapshot().version`（否则 `StaleVersion`），成功后 `++version_`。
- 用户键入等**绕过 apply() 的缓冲变更**：后端连接 `modified` 信号。实测 Scintilla 一次操作会发 3 类伴随通知（`InsertCheck`/`BeforeInsert`/`StartAction`），只有携带 `InsertText|DeleteText` 标志的才是真实内容修改——按此过滤，一次操作恰好一次版本跳变（直接连接 + `mutatingDocument_` 哨兵，无需队列化）。
- `reload()` 重置缓冲与版本；程序化装载（`SCI_SETTEXT`）后必须 `SCI_EMPTYUNDOBUFFER`，否则撤销会穿过装载点回到空缓冲。

### D3 apply() 多编辑：降序局部操作

1. 校验 edits 非空、区间合法（`start <= end <= 文档长度` → 否则 `RangeInvalid`）。
2. 按 start 排序，检查重叠（→ `OverlappingEdits`）。
3. 按 start **降序**逐个执行：`SCI_DELETERANGE(start, end-start)` + `SCI_INSERTTEXT(start, newText)`。降序保证前驱偏移不受影响，无需重映射。
4. 返回新版本。

### D4 搜索：委托 core 语义，M09 不引入 Scintilla target 搜索

`search()` 通过 `read()` 分块（与 FileDocumentBackend 相同的分块边界规则，含多字节边界回退）复用 core 字面量搜索逻辑，保证与契约测试中既有语义（maxResults、truncated、cancel）逐字节一致。SCI_SEARCHINTARGET 加速属 M11（分批结果 + 正则超时）范围。

### D5 locateLines：用 Scintilla 行索引，O(1)

`SCI_LINEFROMPOSITION` + `SCI_GETLINE` 直接换算 `TextPosition{line, column}`（均 1 起，对齐 core 语义），避免重扫缓冲。

### D6 save/saveAs

- `saveAs(path)`：8 MiB 窗口 `SCI_GETTEXTRANGEFULL` 分块读出 → `AtomicFileWriter` 流式写入 → `commit()` 原子替换；`cancelFlag` 在每个块边界检查。
- `save()`：写回当前 `path_`；`info_` 的 BOM/换行样式沿用打开时 `statFile/inspectFile` 的结论，不重推导。
- 换行样式：M09 保持缓冲原样（Scintilla 默认 `SC_EOL_LF` 转换关闭，用 `SCI_SETEOLMODE` 设为检测值），不做重写。

### D7 线程模型

M09 后端为 UI 线程对象（因持有 QWidget 指针）。契约中的 `cancelFlag` 语义在 UI 线程单步内天然满足（操作不可抢占但原子短小）；真正的后台化（分块 Loader、后台搜索）在 M10/M11 通过"后台线程只读 core 快照、结果回 UI 线程提交"实现，本设计不引入跨线程 Scintilla 访问。

### D8 契约测试策略

`tests/scintilla_document_backend_tests.cpp`：offscreen QApplication + 自建（测试专用）ScintillaEditBase 实例注入后端（测试中允许测试自建实例，与 D1 不冲突——生产路径由 GUI 注入可见实例）。复用 `document_backend_tests.cpp` 全部场景：版本变化、StaleVersion、范围错误、重叠、取消、save 往返、reload。CMake 目标 `markdown_qt_scintilla_backend_tests`，链接 `markdown_qt_core + markdown_qt_scintilla + Qt6::Test`。

## 风险与缓解

| 风险 | 缓解 |
| --- | --- |
| 版本计数与信号时序竞态（apply 内触发 modified） | 哨兵位 + 信号排队到事件循环（`QMetaObject::invokeMethod` 队列连接）后在 UI 线程统一处理 |
| BOM 字节在 Scintilla 缓冲中的处理 | 打开时保留 BOM 于缓冲头部，`info_.hasUtf8Bom` 元数据与缓冲一致；往返测试覆盖 |
| 空文档 / 纯 BOM 文档边界 | 契约测试边界用例 |
| GUI 持有的旧快照在 apply 后失效 | M09 内 GUI 每次操作前重新取 snapshot（无缓存），缓存策略留给 M13 DocumentSession |
