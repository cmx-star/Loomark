# M12 验证记录：流式保存与撤销预算 / F02

> 日期：2026-08-29
> 平台：macOS 15.7.7（arm64，Mac mini M4），AppleClang 17，Release

## 验收条件核对（PRD）

| 验收条件 | 结果 | 证据 |
| --- | --- | --- |
| 恢复点单测 | ✅ | `SavePointDirtyTracking` / `SavePointAfterSave`（装载干净→编辑脏→保存干净→重载干净、版本链递增） |
| 外部指纹复核 | ✅ | `ExternalFingerprintReview`：外部改盘 → save 抛「changed on disk」且磁盘字节未动；saveAs 新目标成功；openSync 重建基线后编辑保存恢复 |
| 撤销预算单测 | ✅ | `UndoBudget`（16B 预算：超额编辑触发 canUndo=false、内容不受影响、后续编辑正常） |
| F02 端到端 | ✅ | `loader_verify f02`（300MB 样本）：装载指纹一致 → apply 插入标记 → searchBatch 命中 → 33MiB 替换确认门（拒→确认成功）→ saveAs 流式原子写 279,970,057 字节 1.2s → reload 干净且版本递增 → **F02 VERIFY PASS** |
| 磁盘不足不破坏文件 | ✅（继承） | 保存路径沿用 core AtomicFileWriter + availableDiskBytes 预检（M04 已有专项验证），M12 复核失败发生在写盘前 |
| 全量 ctest / diff-check | ✅ | ctest 9/9；构建 0 错误 0 警告（上游 1 处除外） |

## 关键语义

- 基线指纹统一为**磁盘表示**（含 BOM），装载与保存同口径计算。
- 撤销预算超限时清空整个撤销历史（Scintilla 不支持裁剪最旧动作）；恢复点已落盘，可接受。
- 后端 reload 不再强制 Normal 档（档位策略归 GUI 打开流程）。

## 残余项

- Windows/Linux CI 构建（待推送授权）。
- 撤销预算耗尽的用户引导文案（当前仅状态栏提示）。

## 结论

M12 验收全部满足；**功能门 F02「300MB 桌面完整编辑」端到端达成**，批次 3（M09–M12）完成。
