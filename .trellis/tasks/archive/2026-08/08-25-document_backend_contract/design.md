# 设计：IDocumentBackend 契约

## 已验证事实

- core 现有函数均为「路径 + 参数」自由函数，无状态、无版本概念。
- GUI main_window 自持 `windowStart_/windowEnd_`（窗口字节区间）、`documentGeneration_`（文档代际）、`previewGeneration_`（预览代际），保存时用 `streamCopyRange` 三段拼接 + `AtomicFileWriter`。
- `searchLiteral(path, needle, options)` 全文扫描；`locateByteRange(path, range)` 从文件头线性推进；二者都接受可选取消或无取消（searchLiteral 目前**没有** cancelFlag 参数——契约层需要补）。
- `readRange` 越界抛 `std::out_of_range`；`locateByteRange` 越界抛 `std::runtime_error`。
- core 库 `markdown_qt_core` 无 Qt 依赖（CMakeLists L100-114）。

## 方案

### 新文件与职责

1. `src/core/document_backend.h` — 纯接口 + 值类型
2. `src/core/document_backend.cpp` — `FileDocumentBackend` 实现
3. `tests/document_backend_tests.cpp` — 契约测试

### 值类型（document_backend.h）

```cpp
namespace mqt::core {

using DocumentVersion = std::uint64_t;
inline constexpr DocumentVersion kInitialDocumentVersion = 1;

struct DocumentInfo {           // 复用语义对齐 FileInfo
    FileTier tier;
    std::uint64_t sizeBytes;    // 当前内容大小（编辑后为缓冲大小）
    bool hasUtf8Bom;
    NewlineStyle newlineStyle;
    bool newlineStyleKnown;
};

struct DocumentSnapshot {
    DocumentVersion version = 0;
    DocumentInfo info{};
};

struct TextEdit {               // 单个原子编辑：把 [start,end) 替换为 newText
    std::uint64_t start = 0;    // 相对正文（不含 BOM）的字节偏移
    std::uint64_t end = 0;
    std::string newText;        // UTF-8
};

enum class ApplyError { None, StaleVersion, OverlappingEdits, RangeInvalid };

struct ApplyResult { ApplyError error; DocumentVersion newVersion; };

struct SearchQuery { std::string needle; SearchOptions options; };
struct SearchOutcome { SearchResult result; bool cancelled; };
}
```

### IDocumentBackend 接口

```cpp
class IDocumentBackend {
public:
    virtual ~IDocumentBackend() = default;

    virtual DocumentSnapshot snapshot() const = 0;
    virtual DocumentInfo info() const = 0;

    // version 不匹配当前版本 -> 抛 StaleDocumentError（含期望版本）
    virtual std::string read(ByteRange range) const = 0;
    virtual LocateResult locateLines(ByteRange range) const = 0;
    virtual SearchOutcome search(const SearchQuery& query,
        const std::atomic_bool* cancelFlag = nullptr) const = 0;

    // 所有 edits 的 start/end 必须单调不重叠且落在当前内容内；
    // baseVersion 过期 -> 返回 {StaleVersion,...} 且零改动。
    virtual ApplyResult apply(std::vector<TextEdit> edits,
        DocumentVersion baseVersion,
        const std::atomic_bool* cancelFlag = nullptr) = 0;

    virtual void save(const std::atomic_bool* cancelFlag = nullptr) = 0;
    virtual void saveAs(const std::filesystem::path& path) = 0;
    virtual DocumentSnapshot reload() = 0;
};
```

异常约定：

- `StaleDocumentError`（继承 std::runtime_error）：read/locateLines 用过期版本调用时抛出——本设计简化为 read/locate **不带版本参数**（见下），该异常仅由内部一致性检查使用。最终定案：read/locateLines 不带版本参数，调用方以 snapshot 版本做乐观并发判断；apply/save 是唯一写入口，版本校验集中在那里。这样接口更小，M13 Session 持有 snapshot 即可判断新鲜度。

### FileDocumentBackend 实现要点

- 成员：`path_`、`std::string buffer_`（当前内容，BOM 剥离存储，与 GUI 现行为一致）、`bomOffset_`（0 或 3）、`DocumentInfo info_`、`version_`、`dirty_`。
- 构造即打开：`statFile` 取元数据；Normal 档全文读入；Large/Extreeme 同样读入（参考实现允许，M06/M09 再换流式后端；但 `open()` 提供 `maxResidentBytes` 提示参数？——定案：不做。参考实现明确只服务 ≤64MB Normal 档，超档构造抛错并说明理由：契约测试不需要大文件，避免复制窗口化逻辑到将被替换的实现里）。**修正**：为让测试覆盖 Mixed/CRLF 与 BOM 场景，小文件即可，档位限制合理。
- `read(range)`：直接切 `buffer_`，越界抛 `std::out_of_range`（对齐现有 readRange 行为）。
- `locateLines(range)`：在内存缓冲上线性计算行列（复用 advanceCursor 思路），越界抛错。O(n) 但仅内存扫描。
- `search(query)`：KMP 在 `buffer_` 上执行（复用 buildPrefixTable），支持 cancelFlag 分块取消；返回行列与字节范围。实现方式：临时落盘再调 searchLiteral 会破坏「后端不依赖路径」原则且浪费 IO，故抽公共 KMP 内核供两者复用——**范围控制**：不动现有 document_file.cpp 的 searchLiteral 签名，在 document_backend.cpp 内自含一个缓冲区搜索辅助（代码量小，避免重构风险）。
- `apply(edits, baseVersion)`：
  - 校验 baseVersion == version_，否则 `{StaleVersion}`。
  - 校验 edits 非重叠、0≤start≤end≤buffer_.size()，否则相应错误。
  - 从后向前应用（避免偏移失效），成功后 `++version_`，返回新版本。
  - 编辑作用于剥离 BOM 后的正文坐标；BOM 属于文件头不属于可编辑正文（与 GUI 窗口跳过 BOM 一致）。
- `save(cancelFlag)`：`availableDiskBytes` 预检（needed = buffer+2×+1MiB 对齐 ensureDiskSpace 公式）→ `AtomicFileWriter` 写 BOM(如有)+buffer_ → commit → 更新 info_ 大小、dirty_=false。cancelFlag 只在写入循环间检查（AtomicFileWriter 本身不支持中途取消——保持简单：本任务 save 不接 cancelFlag，接口留默认参数占位，注释说明 M10 引入真取消）。**简化定案**：save/reload 不带 cancelFlag；只有 search/apply 带（apply 是 O(edits) 快操作，其实也不需要——最终接口：仅 search 带 cancelFlag，其余同步快速操作不带）。PRD AC4 的「所有可耗时操作」在本参考实现语境下收敛为 search（唯一可能秒级耗时的操作）；契约头注释声明未来后端扩展取消的约定。
- `reload()`：重新 statFile+读入，`++version_`（即使内容相同也递增，保证外部修改可见性）。
- `saveAs(path)`：写到新路径并把 path_ 切换过去。

### 测试边界（tests/document_backend_tests.cpp）

沿用 core_tests 的 require() 风格，独立 ctest 目标：

1. open+snapshot：BOM/换行/档位/version 初值 1。
2. read 正确性与越界抛错。
3. locateLines 行列正确（含 CRLF、多行）。
4. search 命中、truncated、cancel 立即置位返回 cancelled=true 且 hits 为空。
5. apply：正常替换单点、多点不重叠、新版本号递增、read 回读验证。
6. apply stale：旧版本被拒、内容不变、版本不变。
7. apply 重叠/越界被拒。
8. save→磁盘内容逐字节验证（BOM+CRLF 保持）；save 到只读目录失败不破坏原文件（用不存在父目录路径触发 AtomicFileWriter 异常）。
9. reload：外部改文件后 version 递增、read 反映新内容。

## 影响面

- CMakeLists：core 库加 document_backend.cpp；新增 tests 目标 markdown_qt_document_backend_tests。
- 不触碰 gui/、cli/、既有测试。

## 回退路径

删除 3 个新文件 + 还原 CMakeLists 两处注册，单 commit revert。

## 明确不做

- 不给 searchLiteral/locateByteRange 增加 cancelFlag 重构（避免波及 CLI/GUI）
- 不做撤销栈、undo 组
- 不做 GUI 接线
- 不支持 >64MB 文件的参考实现（契约本身不限）
