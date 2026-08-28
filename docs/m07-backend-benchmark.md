# M07 等价后端对照基准记录

> 日期：2026-08-28
> 设备：Mac mini（Apple M4，10 核，32 GB），macOS 15.7.7
> 工具链：AppleClang 17.0.0，Qt 6（Homebrew），CMake 4.4.2，`CMAKE_BUILD_TYPE=Release`
> 样本：`samples/generated/tier-extreme-300mb.md`（300 MiB，结构化 Markdown，约 99 万个 Section，不提交 Git）
> 方法：`examples/scintilla_prototype/backend_bench.cpp`，两种后端分进程运行（`QT_QPA_PLATFORM=offscreen`），`ru_maxrss` 峰值互不污染；搜索探针为首现位置位于文件后 15% 的唯一字符串 ` " Section 891000"`，保证真实全量扫描语义。单次运行记录，未做多轮取均值。

## 结果

| 指标（300 MiB 样本） | QPlainTextEdit（现状 Widgets 桥接） | ScintillaEditBase（候选后端） | 窗口化 P0 桥接（M04 记录，参考） |
| --- | ---: | ---: | ---: |
| 打开（全文装载） | **32 726 ms** | **337 ms**（≈97×） | 101 ms（仅装载 2 MiB 窗口，无完整编辑） |
| 编辑（中部插入 512 B） | 0.6 ms | 126.6 ms | 窗口内编辑，ms 级 |
| 搜索（远端唯一探针） | 1 961.6 ms | 197.7 ms（命中于 90% 处） | 仅窗口内容可搜 |
| 全文提取 / 保存 | toPlainText 289 ms + 写盘 1 131.9 ms | 分块提取+写盘一体 1 173.4 ms | 858 ms（三段拼接，逐字节一致） |
| 峰值常驻内存 | **2 440 MB**（终值 2 898 MB） | **1 321 MB**（终值 1 661 MB） | **93.9 MiB**（不装载全文） |

## 解读

1. **打开**：QPlainTextEdit 路径需要把 300 MiB 解码成 UTF-16 QString 并建 QTextDocument 结构，耗时 32.7 s——直接违反架构红线「UI 不长期持有完整大文件的第二份字符串副本」，且打开期间 UI 冻结。Scintilla 按字节缓冲装载，337 ms 可用。
2. **内存**：两条路径峰值都包含 ~300 MiB 原始文件缓冲（基准在装载后仍持有 QByteArray；真实应用读完即释放）。差值主要来自 QString 的 UTF-16 双倍膨胀与 QTextDocument 结构。
3. **编辑**：QPlainTextEdit 的增量插入极快（0.6 ms）；Scintilla 中部插入 126.6 ms（行索引重建成本），对交互式单次操作可接受，但「大范围多处插入」场景（M11/M12）需要预算控制。
4. **搜索**：两者都能全量搜索，Scintilla 快约 10 倍且原生支持目标范围、正则与取消语义接入（M11 需要）。
5. **保存**：Scintilla 分块提取写盘（无第二份完整副本）与 Widgets 物化完整 QString 后写盘耗时相当；但 Scintilla 路径满足架构红线的内存约束。

## 结论边界

- 本记录覆盖打开、编辑、搜索、保存、内存五项，满足 M07 验收「形成可复核指标，不以主观流畅度下结论」。
- 未覆盖：全文撤销内存预算（M12）、正则超时（M11）、外部指纹复核（M12）、Windows/Linux 复测。
- 复核命令：
  ```bash
  cmake --build build --target markdown_qt_scintilla_bench
  QT_QPA_PLATFORM=offscreen ./build/markdown_qt_scintilla_bench samples/generated/tier-extreme-300mb.md widgets
  QT_QPA_PLATFORM=offscreen ./build/markdown_qt_scintilla_bench samples/generated/tier-extreme-300mb.md scintilla
  ```
