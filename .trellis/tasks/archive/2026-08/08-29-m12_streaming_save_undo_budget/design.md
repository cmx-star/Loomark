# 设计: M12 流式保存与撤销预算

## D1 恢复点（版本保存点）

- `savedVersion_`：装载完成与每次成功保存后记录当前 `version_`。
- `isDirty() = version_ != savedVersion_`；`save/saveAs/reload` 成功后自动归位。
- 撤销历史在保存成功后 `SCI_SETSAVEPOINT`（对齐 Scintilla 语义，GUI 标题栏可依赖）。

## D2 外部指纹复核

- 基线：`baselineFingerprint_`（装载完成时 = 流式指纹；每次成功保存后 = 刚写入内容的指纹）。
- 保存前复核（仅当目标 == 当前 path_ 且文件存在）：流式读磁盘 8MiB 分块重算指纹，
  与基线不符 → 抛 `runtime_error("document changed on disk since last save")`，
  AtomicFileWriter 尚未创建 → 原文件天然不受影响。
- `saveAs` 到新目标：目标可能本就存在，覆盖即用户意图 → 不复核。
- 成本：300MB 重算 ~70ms（M07 实测原始读取），相对保存 1.2s 可接受。

## D3 撤销内存预算

- `undoBytesUsed_`：onEditorModified（外部键入，按通知 length 累计）与 apply()
  （Σ删除+新增）两条路径增量累计。
- `undoBudgetBytes_` 默认 64MiB，`setUndoBudgetBytes()` 可配置（测试/未来设置页）。
- 超预算：`SCI_EMPTYUNDOBUFFER` + 计数归零 + `undoBudgetExceeded()` 信号。
  语义取舍：Scintilla 不支持裁剪最旧撤销；恢复点（磁盘）已存在，可接受整段清空。
- 版本不受影响（撤销清空不改变文档内容）。

## D4 保存的指纹落定

- 保存写盘时分块提取的同一循环里喂 FingerprintSink → `baselineFingerprint_` =
  写入内容指纹（即用户当前缓冲），保存后 `fingerprint_` 同步为该值。
- 外部键入后 fingerprint_ 保持基线语义（FNV 不可逆，无法增量撤销已删字节）。

## D5 F02 端到端（loader_verify 增加 f02 模式）

1. startBackgroundLoad(300MB 样本) → 等待完成（指纹一致）
2. applyReplace：>32MiB 确认门拒绝 → confirmed=true 成功
3. searchBatch：命中插入的标记串
4. save()：流式原子写；比对磁盘新指纹
5. reload()：重载后缓冲与磁盘一致、版本递增
6. 全程事件循环泵计数，验证无冻结

## 风险

| 风险 | 缓解 |
| --- | --- |
| 外部复核对只读目录抛 I/O 异常 | 复核读失败视为无法验证 → 按「外部修改」拒绝保存并报错（保守正确） |
| 预算计数跨 undo 组重复累计 | SCI_UNDO 触发的 Delete/Insert 通知也计入预算——方向正确（内存确实被占用） |
