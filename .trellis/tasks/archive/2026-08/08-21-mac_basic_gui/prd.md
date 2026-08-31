# 跨平台基础GUI：打开、编辑、预览

## Goal

做出跨平台 Markdown 编辑器的基础 GUI 首版，用户能在应用里打开文件、编辑内容、查看预览。

首版实现优先在 macOS 构建与验证环境上推进，但产品目标本身必须保持跨平台，不把 macOS 当成唯一目标。

## Requirements

- 提供可启动的桌面 GUI 主窗口。
- 支持通过文件打开进入编辑状态。
- 支持编辑区修改并保存当前文档。
- 支持预览区展示当前文档内容。
- 支持编辑与预览在同一窗口内共同工作。
- macOS 作为首个可用构建和验证环境，后续实现不能绑定 macOS 专属行为。
- 主编辑、主预览和 AI 展示不得依赖 WebView。

## Acceptance Criteria

- [x] 启动后能进入基础 GUI 主界面。
- [x] 能打开一个本地 Markdown 文件并在编辑区显示内容。
- [x] 编辑区修改后可保存回文件。
- [x] 预览区能随当前文档显示内容。
- [x] 打开、编辑、预览三者在同一窗口内可见且可操作。
- [x] 同一套产品逻辑不依赖 macOS 专属 API 才能描述和实现。
- [x] 当前版本不使用 WebView 作为主编辑或主预览。

## Out of Scope

- AI 功能
- Git 功能
- 插件系统
- 文章/书籍/导出中心
- 移动端正式适配
- 所见即所得、Diff、并排预览同步

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
