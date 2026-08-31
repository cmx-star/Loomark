# M09 验证记录：ScintillaDocumentBackend 正式桌面后端接入

> 日期：2026-08-29
> 前置：D01 决策已冻结（docs/m08-backend-decision.md，默认批准生效，用户可否决回退）
> 平台：macOS 15.7.7（arm64，Mac mini M4 / 32GB），AppleClang 17，Release

## 验收条件核对（PRD）

| 验收条件 | 结果 | 证据 |
| --- | --- | --- |
| `ScintillaDocumentBackend` 通过契约测试 | ✅ | `markdown_qt_scintilla_backend_tests` 24 用例全绿（镜像 FileDocumentBackend 全部 21 个场景 + 3 个后端特有用例：外部缓冲变更推进版本、apply 单撤销组、空编辑不动版本） |
| 编辑、撤销、选择、保存不经第二份完整 UI 字符串 | ✅ | 事实源 = Scintilla 字节缓冲；保存 8MiB 分块提取直写 AtomicFileWriter；读取分段；GUI 侧 QString 仅预览渲染瞬时转换 |
| GUI NORMAL 档后端路径往返一致 | ✅ | `testNormalBackendSaveRoundTrip`：BOM+CRLF 混合换行文档字节级往返；经编辑器 append→save→文件持久化 |
| QPlainTextEdit 旧路径保留为回退 | ✅ | LARGE/EXTREME 档继续走窗口化 QPlainTextEdit 路径（窗口字节拼接保存、索引预览原样未动） |
| 全量 ctest 通过 | ✅ | ctest 8/8；构建 0 错误 0 警告（上游 Scintilla 1 处 deprecation 除外）；`git diff --check` 通过 |

## 关键实现决策（详见 design.md）

- 位置语义：契约 UTF-8 字节偏移 = Scintilla SC_CP_UTF8 文档偏移，零转换层。
- 版本权威：后端独占计数；外部修改经 `modified` 信号过滤（仅 InsertText/DeleteText）合并为一次跳变；程序化装载后 `SCI_EMPTYUNDOBUFFER`。
- apply：升序校验（对齐参考实现语义）+ 降序执行（免偏移重映射），单次 apply = 单撤销组。
- GUI：QStackedWidget 切换 Normal(Scintilla)/windowed(QPlainTextEdit) 两编辑器，同一时刻仅一个为事实源。

## 过程中发现并修复的问题

1. wrap 模式在编辑器隐藏（栈切换后）时设置，触发对残留 2MiB 窗口文本的同步全量重排版（采样确认 layoutBlock 死循环级耗时）→ 改为仅在隐藏态进入 windowed 前设置 NoWrap。
2. 测试用例自身的编辑顺序违反契约升序要求（后端正确返回 OverlappingEdits）→ 修正用例。

## 残余项

- Windows/Linux 构建验证（CI 项，与 M06 残余合并，待推送授权）。
- 真实 IME / 鼠标拖拽人工 GUI 实测（本环境无显示器捕获）。
- 超限预览的围栏前扫上限 2 万行（更早的未闭合围栏可能误判，记录于代码注释）。

## 结论

M09 全部验收条件满足（残余项均为跨平台/人工项）。批次 3 后续 M10（分块 Loader 与行索引）可以启动。
