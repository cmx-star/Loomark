# M13 DocumentSession 会话层

## Goal

批次4 M13：把「打开的文档」抽象为独立会话对象——每个会话拥有自己的 Scintilla 编辑器、
ScintillaDocumentBackend、路径/档位元数据与任务取消域。切换或关闭文档时旧会话整体退场，
新会话全新建立，从结构上杜绝状态串扰与迟到结果污染。

## Requirements

- `DocumentSession`（GUI 层 QObject）：拥有独立 ScintillaEditBase + 后端实例
- 会话 API：openSync / openBackground / save / saveAs / reload / cancelLoad
- 会话信号：loadProgress / loadFinished / contentsChanged / undoBudgetExceeded（转发+封装）
- 取消域：会话销毁 = 取消在途装载 = 线程确定退出；迟到 chunk 只会落进所属会话的
  自己的编辑器，不可能污染其它会话
- MainWindow 改为持有活动会话；单文档行为不变（M14 在其上叠加标签页）
- 大档位 windowed 回退路径保持不变

## Out of Scope

- 标签页 UI 与多会话并存（M14）
- Workspace/最近文件（M15）、命令系统（M16）

## Acceptance Criteria

- [ ] 会话单测：open/edit/save 往返、openBackground 装载与元数据、取消域
- [ ] 跨会话隔离：会话 A 后台装载中途创建会话 B 并装载，A 的完成/迟到块不影响 B；
      B 的缓冲与 A 无关
- [ ] GUI 回归：现有 main_window_tests 全绿（Normal 档行为不变）
- [ ] 全量 ctest 通过；git diff --check 通过

## Notes
