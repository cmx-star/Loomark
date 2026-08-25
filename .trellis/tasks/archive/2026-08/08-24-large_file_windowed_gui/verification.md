# 验证记录：GUI 大文件窗口化接入

验证日期：2026-08-24

## 参考

- 需求：`prd.md`
- 设计：`design.md`
- 实施切片：`implement.md`
- 总体规划：`development-feature-roadmap.md` M01～M04、F01
- 技术约束：`large-markdown-editor-technical-solution-and-roadmap.md` §4、§8、§13.2

## 环境

- macOS 15.7.7
- Mac mini，Apple M4（10 核），32 GB 内存
- Apple Clang 17.0.0
- Qt 6.11.1
- CMake preset：`macos-debug`

## 自动化检查

```bash
cmake --build --preset macos-debug --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build/macos-debug --output-on-failure
git diff --check
```

结果：

- Debug 构建成功。
- CTest 5/5 通过。
- `git diff --check` 通过。

新增回归覆盖：

- `markdown_qt_window_boundary_tests`：完整 2/3/4 字节 UTF-8 尾字符、残缺 UTF-8、CRLF 前瞻。
- `markdown_qt_main_window_tests`：2 MiB 窗口末尾 UTF-8/CRLF 未修改保存字节不变；LARGE 切换 NORMAL 后旧索引不得覆盖新预览。
- `markdown_qt_core_tests`：段落/代码正文采集、块文本截断、超长行、未闭合围栏、取消、流式写入、替换失败清理。

## 大文件探针

探针入口：

```bash
QT_QPA_PLATFORM=offscreen \
MQT_MATHJAX_BUNDLE=build/macos-debug/mathjax-runtime/mathjax_bundle.js \
build/macos-debug/markdown_qt_main_window_tests --probe <source> [save-target]
```

样本位于 `samples/generated/`，不纳入 Git。

| 样本 | 档位 | 打开 | 索引 | 编辑器字符 | 最大常驻内存 | 保存 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| 70 MiB | LARGE | 108 ms | 94 ms | 2,097,152 | 91.6 MiB | 未执行 |
| 300 MiB | EXTREME | 101 ms | 93 ms | 2,097,152 | 93.9 MiB | 858 ms，另存结果逐字节一致 |
| 512 MiB | EXTREME | 839 ms | 1,913 ms | 2,097,152 | 172.9 MiB | 未执行 |
| 520 MiB | REJECT | 12 ms | 不适用 | 不适用 | 53.0 MiB | 拒绝打开 |

512 MiB 样本为稀疏超长行文件，因此索引扫描完整 512 MiB；预览元信息显示块文本已截断。70 MiB 和 300 MiB 结构化 Markdown 样本达到 800 块上限后提前结束索引。

## 磁盘不足

使用临时 16 MiB HFS+ 磁盘镜像，把可用空间压缩到 1 MiB，再把 1 MiB 文档另存到已有目标：

- 保存返回失败。
- 原目标内容保持不变。
- 临时文件未替换原目标。
- GUI 文案：`磁盘可用空间不足，无法安全保存：本次约需 3 MiB，当前仅剩 1 MiB。`
- 验证结束后磁盘镜像已卸载并清理。

## 结论边界

本记录证明 F01 的分档、2 MiB 窗口读取、后台索引、迟到结果隔离和流式安全保存链路。它不证明 F02 的 300 MiB 完整编辑、全文搜索、完整撤销或正式 Scintilla 后端。
