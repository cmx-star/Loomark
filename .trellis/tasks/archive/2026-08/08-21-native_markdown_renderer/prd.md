# 原生 Markdown AST 渲染器

## Goal

用 `md4qt` AST 与 `QTextDocument` 原生渲染替换当前 `QTextBrowser::setMarkdown()` 主预览，在不依赖 WebView 的前提下获得可扩展的 Markdown 解析与渲染边界，并保留现有大文件窗口化预览路径。

## Background

- 当前 GUI 使用 `QPlainTextEdit` 编辑、`QTextBrowser::setMarkdown()` 预览，并以 60 ms 防抖刷新。
- 当前完整预览上限约为 256 KiB 字符；更大内容按编辑区位置截取约 128 KiB 字符后预览。
- 项目规则要求主编辑、主预览和 AI Markdown 展示不得依赖 WebView。
- 用户已批准用原生 AST 渲染替换当前预览，明确本轮不处理 WebEngine，Mermaid 延后并记录到 README。

## Requirements

- R1：引入官方 `md4qt` 静态 C++ 解析库，固定到声明版本 `5.1.3` 的提交 `1a878810adeeb534de4de395f4951edd54ab6072`。
- R2：新增独立的原生 Markdown 渲染模块，将 `md4qt` AST 写入调用方提供的 `QTextDocument`，GUI 不直接遍历第三方 AST。
- R3：主预览不得调用 `QTextBrowser::setMarkdown()`，不得生成 HTML 后交给 WebView，也不得新增 WebEngine；用户已批准引入 MathJax/QuickJS 仅用于 TeX 到 SVG 的公式转换。
- R4：首版原生渲染至少覆盖标题、段落、粗体、斜体、删除线、软/硬换行、行内代码、围栏代码块、引用、无序/有序/任务列表、链接、图片、表格和水平分隔线。
- R5：原始 HTML 不执行，按可读纯文本处理；链接点击继续沿用 `QTextBrowser` 的外部链接行为。
- R6：数学 AST 使用打包后的 MathJax bundle 在嵌入式 QuickJS 中将 TeX 转成 SVG，再由 QtSvg rasterize 后嵌入 `QTextDocument`；转换失败时保留可读源码降级。
- R7：保留现有 60 ms 防抖、完整/窗口化预览阈值、滚动同步提示和打开/编辑/保存行为。
- R8：README 记录新预览架构、固定依赖、首次配置需要联网、WebEngine 不在当前路线中，以及 Mermaid 为后续事项。
- R9：保留工作区现有未提交 GUI、主题和 Trellis 变更，不回退或重写无关内容。

## Acceptance Criteria

- [x] CMake 首次配置能从官方仓库取得固定提交，后续构建链接 `md4qt::md4qt`，并关闭依赖自身测试、工具与安装目标。
- [x] 常用 Markdown fixture 经新渲染模块生成非空 `QTextDocument`，标题、行内样式、代码块、列表、引用、表格、链接和分隔线具有可验证的原生文档结构或格式。
- [x] 原始 HTML 不被当作富文本执行；数学表达式通过 MathJax SVG 图片显示，失败时内容仍然可见。
- [x] GUI 预览刷新不再调用 `setMarkdown()`，但小文档整篇预览和大文档动态截取行为保持。
- [x] 桌面 GUI 仍能打开、编辑、预览并通过核心原子写入路径保存文件。
- [x] README 明确说明原生 AST 渲染路线，并把 Mermaid 标为后续能力。
- [x] `cmake --preset macos-debug`、`cmake --build --preset macos-debug`、`ctest --preset macos-debug` 通过。
- [x] GUI 启动检查通过，预览区对代表性 Markdown 文档可见且无 WebEngine 进程或依赖。

## Out of Scope

- WebEngine、Marked.js、QWebChannel 和任何 HTML/JavaScript 预览路径。
- Mermaid 解析或图形渲染。
- WebEngine/浏览器内核公式渲染路径；MathJax 仅作为受控 TeX 到 SVG 转换器运行。
- 编辑器控件替换、Scintilla/QScintilla 接入。
- 后台增量 AST 解析和跨线程渲染调度；本轮继续限制每次解析的预览文本规模。
