# 性能门禁基线（M68）

> 设备：Mac mini（Apple M4，10 核，32GB）| macOS 15.7.7 | Release
> 样本：`samples/generated/tier-extreme-300mb.md`（300 MiB，约 99 万 Section）
> 复核工具：`markdown_qt_scintilla_loader_verify` / `markdown_qt_scintilla_bench`

| 指标 | 基线 | 阈值（回归门禁） |
| --- | ---: | ---: |
| 300MB 后台装载总时长 | 845 ms | ≤ 2 s |
| 装载期间 UI 事件循环最大间隔 | 61 ms | ≤ 500 ms |
| 装载后指纹一致性 | 一致 | 必须 |
| 取消响应 | 12 ms | ≤ 200 ms |
| 同步 SCI_SETTEXT 打开 | 337 ms | ≤ 1 s |
| 全文搜索（远端探针，searchBatch 分批） | ~5 ms/批 | ≤ 2 s 全文 |
| 保存（300MB 级流式原子写） | 1.2–2.9 s | ≤ 5 s |
| QPlainTextEdit 基线（对照，不作为产品门禁） | 打开 32.7s / 2.4GB | 仅参考 |

回归测试在 CI 中以固定设备/样本执行；本地复核命令见 docs/m07-backend-benchmark.md。
