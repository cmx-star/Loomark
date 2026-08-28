# 设计: M11 分批搜索替换

## 架构

### D1 SearchBatch（backend，UI 线程）

```cpp
struct SearchBatchRequest {
    std::string needle;              // UTF-8
    bool regex = false;              // SCFIND_REGEXP
    bool matchCase = true;
    bool wholeWord = false;
    std::uint64_t startOffset = 0;   // 批起点（字节）
    std::uint32_t maxResults = 1000; // 单批上限
    std::uint32_t maxWindow = 1MiB;  // 单批扫描窗口上限
    std::uint64_t deadlineMs = 250;  // 单批时间预算
};
struct SearchBatchResult {
    std::vector<SearchHit> hits;     // 复用 core::SearchHit（含 TextPosition）
    bool truncated = false;          // maxResults 截断
    bool timeout = false;            // deadline 到
    bool exhausted = false;          // 已扫到文档末尾
    std::uint64_t nextOffset = 0;    // 下一批起点（= 最后命中末尾）
};
SearchBatchResult searchBatch(const SearchBatchRequest&, const std::atomic_bool* cancel);
```

- 实现：`SCI_SETTARGETSTART/END(startOffset, min(startOffset+maxWindow, len))` + `SCI_SEARCHINTARGET`，
  循环推进 target 起点收集命中，直到 maxResults / 窗口尾 / deadline / cancel。
- deadline：每批 deadlineMs 由调用方轮询检查（批内单次 SCI_SEARCHINTARGET 最坏
  卡顿有 maxWindow=1MiB 上限兜底，灾难性回溯不会冻结 UI）。
- 取消：批间循环每步检查；正则单步不可中断由窗口上限兜底。
- 行/列：命中位置经 SCI_LINEFROMPOSITION 换算（对齐契约 1 起、字节列）。

### D2 替换与成本确认（backend）

```cpp
struct ReplacePlan { std::vector<TextEdit> edits; std::uint64_t affectedBytes; };
ReplacePlan planReplace(request, matches范围) // 由调用侧从命中构造
ApplyResult applyReplace(const std::vector<TextEdit>&, DocumentVersion base, bool confirmed);
```
- `applyReplace`：先算 affectedBytes = Σ(edit.end-edit.start + newText.size())（相对原缓冲）；
  affectedBytes > 32MiB 且 !confirmed → 返回 {ApplyError::None, version} 前先拒绝：
  引入 `ApplyError::ConfirmationRequired`（core 枚举扩展，兼容现有测试）。
- 执行即复用 `apply()`（升序校验/降序执行/单撤销组/版本递增）。
- 常数：`inline constexpr std::uint64_t kReplaceConfirmBytes = 32ULL << 20;`

### D3 GUI 查找/替换栏

- Normal 档底部隐藏查找栏（Ctrl+F 呼出）：needle 输入、大小写/正则复选、
  查找下一个、全部替换。全部替换 = 循环 searchBatch 收集全部命中（批上限内）→
  planReplace → affectedBytes > 32MiB 时 QMessageBox::question 确认 → applyReplace(confirmed)。
- 收集循环可取消（Esc / 关闭栏）；结果数显示在栏内。
- Windowed（large）档：本模块不提供搜索 UI（保持 F01 行为），M12 后再统一。

### D4 测试

- backend 单测：分批续扫（3 批扫完、命中不重不漏）、单批上限截断、deadline 超时标志、
  cancel 中断、正则模式基础匹配、替换成本确认门（>32MB 拒绝 / confirm 成功）、
  替换后版本/撤销组行为。
- 300MB 实测：searchBatch 远端探针收集 + cancel 及时性（loader_verify 风格工具）。
