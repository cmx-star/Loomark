# 设计: M13 DocumentSession

## D1 会话 = 编辑器 + 后端 + 元数据 + 取消域

每个会话创建**自己的** ScintillaEditBase（父对象为会话）与 ScintillaDocumentBackend
（父对象为编辑器）。隔离由所有权结构保证：

- 迟到结果：装载 chunk 经 queued 信号送达「后端所属会话」的槽 → 只进该会话的编辑器。
  销毁会话 = delete 后端与编辑器 → 连接断开、在途任务取消、线程 join。
- 状态串扰：路径/档位/脏状态/行索引/指纹全部位于会话内，无全局可变共享。

## D2 取消域

- 会话析构 → 后端析构 → ScintillaLoadTask 析构（cancel + wait）。
- `cancelLoad()` 显式取消但不销毁会话（同会话重开场景）。
- GUI 层 `backgroundLoadPending_` 与会话的 `isLoadInProgress()` 合一：GUI 状态由
  会话信号驱动。

## D3 MainWindow 适配（单活动会话）

- `activeSession_`（M14 扩展为会话列表 + 标签索引）。
- 打开文档 = 关闭旧会话（delete，取消其任务）→ 新建会话 → 加入 editorStack_。
- 原先引用 `scintillaEditor_`/`documentBackend_` 的路径（查找栏、预览、状态栏、
  保存守卫）全部改经活动会话转发；windowed 路径不变。
- editorStack_ 仅含活动会话编辑器 + legacy 编辑器两页。

## D4 测试

- 会话级（offscreen）：openSync 往返、openBackground 内容/元数据/取消、
  跨会话隔离（A 装载中建 B，双方内容互不污染、A 完成不影响 B）。
- GUI 回归：main_window_tests 不改行为断言（仅 appendToEditor 访问器改走会话）。
