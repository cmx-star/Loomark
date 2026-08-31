# M10 验证记录：分块 Loader 与稀疏行索引

> 日期：2026-08-29
> 平台：macOS 15.7.7（arm64，Mac mini M4 / 32GB），AppleClang 17，Release

## 验收条件核对（PRD）

| 验收条件 | 结果 | 证据 |
| --- | --- | --- |
| 稀疏行索引/指纹 core 单测 | ✅ | `markdown_qt_load_scanner_tests` 6 组：基本行数、stride 锚点、跨块、空文档边界、流式指纹=一次性指纹、换行统计（跨块 CRLF/尾 CR） |
| 后台装载缓冲与文件字节一致 | ✅ | 冒烟用例（BOM+CRLF 200KB）read 全文比对；300MB 指纹比对一致 |
| 300MB 后台装载不冻结 | ✅ | `loader_verify load`：总时长 845ms，事件循环泵 762,047 次、**最大单次间隔 61ms**（< 500ms 阈值），指纹一致，行数 9,899,214 |
| 取消后资源释放 | ✅ | `loader_verify cancel`：取消后 12ms 内 loadFinished 送达（ok=false）、缓冲清空（len=0）；单测含重复装载/取消 3 轮循环 |
| locateLines/search 语义一致 | ✅ | 契约测试 24 用例在装载路径重构后仍全绿（同步/后台装载共享同一缓冲语义） |
| 全量 ctest / diff-check | ✅ | ctest 9/9；0 错误（上游 1 处 deprecation 除外）；diff-check 通过 |

## 实现要点

- `ScintillaLoadTask`（QThread）：4MiB 块读取 + core 扫描器在 worker 线程执行；`QSemaphore(2)` 背压限制在途块内存峰值（≤ 2×块大小）；chunkReady 经 queued 连接回 UI 线程 `SCI_APPENDTEXT`（毫秒级）。
- BOM：worker 首块剥离，缓冲不含 BOM（M09 语义），进度按文件原始字节计。
- GUI：Normal 档 >1MiB 走后台装载；装载中禁保存、状态栏进度；完成或失败后统一恢复。
- 已知限制：超限预览的围栏前扫上限 2 万行（M09 遗留，未扩大）。

## 残余项

- Windows/Linux CI 构建（待推送授权）。
- GUI 端到端 Large/Extreme 档切换到后台装载 + 全量编辑（M12 F02 收口）。

## 结论

M10 验收全部满足。
