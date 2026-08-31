# M11 验证记录：分批搜索替换

> 日期：2026-08-29
> 平台：macOS 15.7.7（arm64，Mac mini M4），AppleClang 17，Release

## 验收条件核对（PRD）

| 验收条件 | 结果 | 证据 |
| --- | --- | --- |
| 分批续扫正确、单批上限与截断标志 | ✅ | `SearchBatchPagination`（25 命中 / 上限 10 → 3 批不重不漏）、`SearchBatchTruncatedFlag` |
| 正则可用且超预算返回 timeout | ✅ | `SearchBatchRegexAndTimeout`（20000 行正则扫描，deadline 1ms 触发 timeout 标志） |
| 替换语义不变、单撤销组 | ✅ | 替换委托 apply()；契约既有版本/范围/重叠用例全绿；M09 单撤销组用例仍通过 |
| estimateReplace/确认门：>32MiB 拒绝 | ✅ | `ReplaceConfirmationGate`（33MiB 文档全量替换：未确认 → ConfirmationRequired 且版本不变；确认 → 成功且版本递增）、`SmallReplaceNeedsNoConfirmation` |
| 300MB 搜索可取消 | ✅ | `loader_verify search`：6 批 39,960 命中 5ms；预置取消标志立即返回 cancelled=true |
| 全量 ctest / diff-check | ✅ | ctest 9/9；clean 构建 0 错误 0 警告（上游 1 处除外） |

## GUI

- 查找/替换栏（Ctrl+F 呼出）：正则与大小写开关、查找下一个（环绕）、全部替换
  （分批收集每批 processEvents 可取消、100k 命中与 10s 收集双上限、
  >32MiB 时 QMessageBox 确认后执行）。
- 仅 Normal 档启用（windowed 档维持 F01 行为，M12 收口）。

## 残余项

- Windows/Linux CI 构建（待推送授权）。
- 搜索命中高亮指示器（SCI indicator）延后。
- 正则替换的捕获组引用未实现（替换文本按字面量处理）。

## 结论

M11 验收全部满足。
