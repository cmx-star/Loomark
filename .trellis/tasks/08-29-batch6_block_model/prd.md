# 批次 6：统一块模型与增量索引（M21–M24 → G02）

## Goal

目录、预览、Diff、AI 共用同一份版本化块语义：`MarkdownBlock`（blockId、documentVersion、
sourceRange、headingPath、hash、payload），自研行级块解析适配层，跨块安全检查点
（围栏/引用/列表收敛），版本化索引任务调度（取消、迟到结果丢弃）。

## Acceptance（按用户调整口径：编译无错 + ctest + 打包产物）

- [ ] 块解析：标题/段落/围栏/列表/引用/表格/HR/HTML 块切分正确，含未闭合围栏
- [ ] 安全检查点：从最近检查点向后重扫可收敛
- [ ] 调度器：过期版本结果丢弃、取消立即返回、连续编辑不覆盖
- [ ] ctest 通过；打包产物生成
