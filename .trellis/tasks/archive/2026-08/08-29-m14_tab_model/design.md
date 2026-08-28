# 设计: M14 标签页模型

## D1 结构

- `QList<DocumentSession*> sessions_`；`activeSession_` 指向其一。
- `QTabBar* tabBar_`（setMovable(true)、setTabsClosable(true)）置于 splitter 上方；
  中央布局改为 VBox{tabBar_, splitter}。
- editorStack_ 页面：0 = legacy 编辑器（windowed/空态），1..N = 会话编辑器
  （页序随标签序）。切标签 = setCurrentIndex 到会话编辑器页，零拷贝。

## D2 打开与去重

- loadDocument Normal：`sessionForPath(path)` 命中 → 激活既有会话（若当前在
  windowed 则 largeMode_=false 回 Normal 档）→ 返回，不重复装载。
- 新文档：新建会话 → addWidget → 追加标签 → 激活。
- Large 档：largeMode_=true（标签栏隐藏）；既有 Normal 标签保留，切回即用。

## D3 关闭与未保存确认

- tabCloseRequested → closeSession(s)：脏 → QMessageBox 保存/放弃/取消；
  保存失败或取消 → 中止关闭。
- closeEvent：逐个脏会话激活并走 maybeSaveChanges，全部干净后才接受关闭。

## D4 标记与固定

- 脏标记：tab 文本前缀 "• "；contentsChanged/保存后刷新。
- 固定：tab 右键菜单「固定/取消固定」→ 会话移到列表首、标签前移（pinned 标记存
  tab UserRole）。

## D5 测试（TestAccess 扩展）

- sessionCount / activateSessionAt / sessionPathAt / isSessionDirtyAt。
- 用例：双开不串内容、去重（同路径两开 = 一标签）、关闭脏标签确认路径。
