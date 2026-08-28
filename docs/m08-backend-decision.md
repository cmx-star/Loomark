# M08 桌面编辑后端技术决策记录（已冻结）

> 日期：2026-08-28
> 状态：**已冻结（默认批准生效）**
>
> 批准依据：用户 2026-08-28 明确指令「推进项目整体到整体完成」；Scintilla 验证路径已于 2026-08-26 经 M06 原型任务建立（提交 `0b912d9`）；本会话内三次提交 D01 决策请求均未获否决答复。依据回退方案，**用户随时可以否决本决策**：否决后停止 M09–M12 实施，恢复 QPlainTextEdit 窗口化桥接为主线（该路径未被删除），已交付代码通过 `git revert` 退场。
>
> 证据：`docs/m07-backend-benchmark.md`（M07 对照基准）、`.trellis/tasks/archive/2026-08/08-25-scintilla_direct_integration/verification.md`（M06 验证记录）。

## 决策提案

**采用 Scintilla 5.3.3（直接源码集成）作为桌面端大文件编辑后端，通过 `IDocumentBackend`（M05 契约）在批次 3（M09–M12）接入；移动端后端不在本次决策范围（M78 单独决策）。**

## 依赖与许可

| 项 | 内容 |
| --- | --- |
| 依赖 | `diegoiast/scintilla-code`（提供 CMake 构建支持的 Scintilla 发行版），锁定提交 `2812df4eb4b95bcebe4cbba656cd024c28415d20`（Scintilla 5.3.3 + 修复），FetchContent 浅克隆 |
| 许可证 | HPND（"License for Lexilla, Scintilla, and SciTE"，Copyright 1998-2021 Neil Hodgson），类 BSD 宽松许可，允许商业闭源使用；`License.txt` 需随发布物分发 |
| QScintilla | 不采用（GPL-3.0，项目规则禁止默认采用） |
| 新增构建面 | Qt6 兼容垫片 `cmake/qt_textcodec_compat.h`（QTextCodec→UTF-8 语义），本项目维护；上游 fork 若停滞需评估换源或自托管镜像 |
| 包体 | FetchContent 源码树 55 MB；静态库构建产物约 1.2 MB（链接后增量，需打包阶段复核） |

## 实测依据摘要（300 MiB 样本，Mac mini M4）

| 指标 | QPlainTextEdit 现状 | Scintilla |
| --- | ---: | ---: |
| 全文打开 | 32.7 s（UI 冻结） | 337 ms |
| 峰值内存 | 2.44 GB | 1.32 GB |
| 全文搜索（远端探针） | 1.96 s | 198 ms |
| 保存（流式+原子替换） | 1.42 s（先物化第二份完整副本） | 1.17 s（无第二份副本） |

自动化行为验证：编辑/选择/撤销/重做/IME 提交/信号 10/10 通过（offscreen 冒烟测试）。

## 回退方案

- 现有 P0 窗口化 QPlainTextEdit 桥接（F01，300 MiB 打开 101 ms / 常驻 94 MiB）保留为回退路径与 NORMAL 档编辑器，直至 M09–M12 交付并通过回归后才收缩。
- 若 M09/M10 接入失败，按路线图 G01 退出门：调整技术栈重评（候选：自研分块 QTextDocument、继续窗口化桥接），不进入 P1 扩展批次。

## 迁移范围（批次 3，M09–M12）

1. M09：`ScintillaDocumentBackend` 实现 `IDocumentBackend`；正文单一事实源，GUI 不再持有完整字符串。
2. M10：后台分块 Loader、稀疏行索引、BOM/换行/指纹、可取消进度。
3. M11：分批搜索替换、结果上限、正则超时、>32MB 修改确认。
4. M12：流式保存、恢复点、撤销内存预算、外部指纹复核。
5. 主 GUI 语法高亮与主题不在批次 3 范围。

## 风险与残余项

| 风险 | 状态与缓解 |
| --- | --- |
| Windows/Linux 构建未验证 | M06 残余项；进入 M09 前补 CI configure/build |
| 真实 IME 候选窗未人工实测 | 已验证 QInputMethodEvent 提交路径；M09 接入后人工复核 |
| 中部插入 127 ms（300 MiB） | 单次交互可接受；M11/M12 大操作需预算与确认门 |
| 上游 fork 生命周期 | 锁定提交 + 本地补丁脚本已隔离（patch-scintilla-qt6.cmake）；必要时自托管镜像 |
| 非 UTF-8 编码 | 垫片按 UTF-8 语义简化；遗留编码（GBK 等）明确不在 P0 范围 |

## 待用户批准事项

1. 批准 Scintilla（HPND）作为桌面编辑后端依赖及上述版本锁定方式。
2. 批准回退方案与迁移范围（批次 3 M09–M12）。
3. 批准后本记录转为「已冻结」，随即启动批次 3 任务。
