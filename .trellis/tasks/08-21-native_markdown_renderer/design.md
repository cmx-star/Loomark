# 原生 Markdown AST 渲染器设计

## 依赖决策

- Markdown 解析依赖：KDE `md4qt`，上游项目版本 `5.1.3`，MIT 许可证。
- 来源：官方仓库 `https://invent.kde.org/libraries/md4qt.git`。
- 固定版本：提交 `1a878810adeeb534de4de395f4951edd54ab6072`，避免跟随 `master` 漂移。
- 接入：CMake `FetchContent`；关闭 `BUILD_MD4QT_TESTS`、benchmark、工具和安装目标，只链接 `md4qt::md4qt`。
- 公式渲染依赖：npm `mathjax` 4.1.3，Apache-2.0 许可证；用户批准后接入。
- 公式显示接入：Node helper 调用 MathJax 将 TeX 转 SVG，C++ 侧通过 QtSvg `QSvgRenderer` rasterize 成 `QImage` 并以 `QTextDocument::ImageResource` 嵌入，主预览不使用 WebEngine。
- 安装命令：首次公式预览构建前运行 `npm install`；首次执行 `cmake --preset macos-debug` 时由 CMake 获取固定 `md4qt` 提交。

选择 `FetchContent` 是因为当前 Homebrew 没有 `md4qt` formula，上游也没有 Git tag。相比复制数十个上游源文件或增加 Git submodule，这种方式改动较小且提交固定；代价是首次配置需要访问上游仓库，离线全新构建需要预先填充 CMake FetchContent 缓存。MathJax 通过 `package-lock.json` 锁定 npm 解析版本，macOS bundle 会复制 helper、`node_modules` 和许可证；运行环境缺少 Node 或转换失败时退回源码文本。

## 模块边界

新增 `src/gui/markdown_document_renderer.*`：

- 输入：Markdown `QString`、文档相对资源基准路径、目标 `QTextDocument`。
- 解析：通过 `MD::Parser` 的 `QTextStream` 重载生成 AST，禁止递归解析链接文档。
- 输出：用 `QTextCursor`、`QTextBlockFormat`、`QTextCharFormat`、`QTextList`、`QTextTable` 和 `QTextImageFormat` 构造原生文档；数学 SVG 先 rasterize 为图片资源。
- 安全：原始 HTML 作为纯文本插入，不调用 `setHtml()`；不执行脚本；不主动下载网络资源。
- 失败：渲染函数返回结果状态；解析或渲染失败时，GUI 显示可读错误文本，不影响编辑和保存。

`MainWindow` 只负责收集当前完整或窗口化 Markdown、设置文档宽度、调用渲染器以及保持滚动逻辑，不直接依赖 AST 具体节点。

## 渲染规则

- 块元素：标题、段落、引用、列表、代码块、表格和分隔线映射到对应 `QTextDocument` 结构。
- 行内元素：粗体、斜体、删除线、代码、链接通过嵌套 `QTextCharFormat` 合并。
- 图片：保留 URL/相对路径并使用文档基准 URL；首版不新增网络下载器。
- 任务列表：使用 Unicode 复选框前缀并保留列表层级，不新增交互状态。
- 数学：优先用 MathJax 渲染 TeX SVG 并嵌入图片资源；超时、缺少 Node/MathJax 或 SVG 无法 rasterize 时，保留表达式源码并应用区别于正文的样式。
- Mermaid：代码围栏仍按代码块展示，README 明确后续再增加图形渲染。

## 大文件兼容

本轮不把整篇大文档交给 `md4qt`。继续复用 `MainWindow::collectPreviewText()` 的 256 KiB/128 KiB 边界和 60 ms 防抖，只解析当前交给预览区的文本。这样替换解析器时不扩大单次 UI 阻塞的上界。后台增量 AST 与精确源位置同步留到后续任务。

## 测试策略

- 新增独立渲染器测试，使用包含常用块和行内语法的固定 Markdown 字符串。
- 断言纯文本内容、标题格式、列表、表格、链接格式、代码格式、HTML 安全降级，以及 MathJax 公式图片资源。
- 保留并运行现有核心测试。
- 构建并启动 macOS GUI，用代表性 Markdown 文件进行可见预览检查。

## 风险与回退

- 上游 API 没有稳定 tag：用提交哈希固定，并在 README 记录。
- 首次配置依赖网络：失败时明确报告；可通过已有 `_deps` 缓存离线复用。
- 自研渲染器可能存在语法显示缺口：用 fixture 测试覆盖首版清单，未覆盖语法显示为可读文本。
- 回退方式：移除新渲染模块和 `md4qt` 链接，恢复 `QTextBrowser::setMarkdown()`；不涉及文档文件格式或用户数据迁移。
