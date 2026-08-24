# Research: MathJax SVG in QTextDocument

- Query: MathJax + SVG 嵌入 QTextDocument + 不使用 WebEngine 的实现路径、API、影响文件、风险和验证点
- Scope: mixed
- Date: 2026-08-21

## Findings

### Files Found

- `AGENTS.md`: 项目规则要求主编辑、主预览和 AI Markdown 渲染不得依赖 WebView，并要求新增依赖前说明包、原因、风险、替代方案和安装命令。
- `.trellis/workflow.md`: 标准工作流要求依赖变更、跨模块/多文件写入前取得明确批准，并保留 Git 现状证据。
- `.trellis/spec/company/engineering.md`: 依赖优先使用已有依赖；新增/升级/移除依赖前必须说明目标包、原因、风险、替代方案和安装命令。
- `.trellis/spec/company/quality.md`: 验证必须以当前检查证据为准，不能把静态结构外推为用户可见行为。
- `.trellis/spec/company/security.md`: 供应链要求依赖来源、版本和安装命令可核验，不执行来源不明脚本。
- `.trellis/tasks/08-21-native_markdown_renderer/prd.md`: 旧 R3/R6 明确“不新增 Node 或 JavaScript 运行时”“数学只显示源码”，用户最新批准已经改变这部分范围。
- `.trellis/tasks/08-21-native_markdown_renderer/design.md`: 旧设计把数学作为可读源码，公式排版留作独立依赖决策。
- `CMakeLists.txt`: 当前 GUI 只 `find_package(Qt6 COMPONENTS Widgets REQUIRED)`，渲染器和测试都链接 `Qt6::Widgets`、`md4qt::md4qt`。
- `README.md`: 当前说明 native Markdown preview 解析 `md4qt` 并写 `QTextDocument`，数学表达式仍显示源码，且 WebEngine/Marked.js/QWebChannel 不在当前路径。
- `src/gui/markdown_document_renderer.h`: 当前渲染入口只有 `renderMarkdownDocument(QTextDocument&, QString, QUrl)`，没有异步或外部进程接口。
- `src/gui/markdown_document_renderer.cpp`: 当前 `MD::ItemType::Math` 分支把 `$...$` 或 `$$...$$` 直接作为 styled source 插入；图片分支已经使用 `QTextImageFormat`。
- `src/gui/main_window.cpp`: `refreshPreview()` 收集窗口化文本后调用 `renderMarkdownDocument(*preview_->document(), ...)`；预览控件为 `QTextBrowser`，未走 WebEngine。
- `tests/markdown_document_renderer_tests.cpp`: 当前测试断言数学源码可见、HTML 不执行、本地图片用 `QTextImageFormat`、代码块背景为 `#15171a`。

### Code Patterns

- 当前 GUI 预览限制保留在 `MainWindow::collectPreviewText()`，小文档全量，超过 256 KiB 时窗口化到约 128 KiB，见 `src/gui/main_window.cpp:50`、`src/gui/main_window.cpp:516`。
- 当前预览刷新入口集中在 `MainWindow::refreshPreview()`，渲染前后调用 `updatePreviewLayout()`，实际渲染调用在 `src/gui/main_window.cpp:477`。
- 当前 `QTextDocument` 由 `renderMarkdownDocument()` 清空、设置 `baseUrl`、通过 `MD::Parser` 解析后交给 `NativeDocumentRenderer`，见 `src/gui/markdown_document_renderer.cpp:569`。
- 当前数学切入点非常集中：`MD::ItemType::Math` 在 `renderInline()` 内处理，读取 `math->isInline()` 和 `math->expr()`，见 `src/gui/markdown_document_renderer.cpp:286`。
- 当前本地图片嵌入使用 `QTextImageFormat::setName()` + `cursor.insertImage()`，见 `src/gui/markdown_document_renderer.cpp:350`。
- 当前代码块色块由 `QTextBlockFormat` 背景 `#15171a` 控制，行内代码由 `QTextCharFormat` 背景 `#3a414a` 控制，见 `src/gui/markdown_document_renderer.cpp:44` 和 `src/gui/markdown_document_renderer.cpp:371`。
- 当前代码块顶部/底部间距分别为 `12`/`16`，见 `src/gui/markdown_document_renderer.cpp:373`；如果用户说“上边的增加间距”指代码块上方，应改 `block.setTopMargin(...)`。
- 当前主界面深色主题正文背景约 `#1b1c20`，panel 背景 `#202124`，见 `src/gui/main_window.cpp:613` 和 `src/gui/main_window.cpp:633`；代码块 `#15171a` 与主题接近，确实会显得不明显。

### Recommended Implementation Direction

- 推荐保留 native preview：`md4qt` AST 仍负责识别数学节点，MathJax 只作为 TeX -> SVG 转换器，不引入 WebEngine，不把 Markdown 生成 HTML。
- 最小可靠链路：新增一个 Node CLI helper，例如 `tools/mathjax-renderer.mjs`，从 stdin 接收 JSON 数组 `{id, tex, display}`，输出 JSON `{id, ok, svg|error}`；C++ 用 `QProcess` 批量调用，避免每个公式启动一次 Node。
- 渲染器内新增一个 MathJax 适配层，例如 `src/gui/mathjax_svg_renderer.*`，职责是：
  - 扫描当前窗口化 Markdown AST 里的数学节点时调用转换器或缓存结果。
  - 用稳定 key 缓存 `tex + display + themeColor + pointSize/containerWidth` 到 SVG/QImage。
  - 转换成功时 `target_.addResource(QTextDocument::ImageResource, QUrl("mathjax://..."), imageVariant)`，再用 `QTextImageFormat` 插入。
  - 转换失败时退回当前 styled source，保留可读性和失败路径。
- 对 `QTextDocument` 嵌入，Qt 官方支持 `addResource(ImageResource, url, QVariant(image))` 后用 `QTextImageFormat::setName()` 插图；MathJax 生成的 SVG 可以先用 QtSvg 渲染成 `QImage/QPixmap` 再加资源，稳定性高于直接把 SVG 字节交给 rich text 加载。
- 若直接使用 SVG 字节或 `QTextImageFormat` 指向 SVG URL，需要确认当前 Qt 安装的 imageformats/svg 插件可用；更稳的工程方案是 CMake 加 `Qt6::Svg` 并在 C++ 侧用 `QSvgRenderer` 渲染成图片。
- MathJax 侧建议用 `mathjax` npm 包，而不是 `@mathjax/src`，因为用户已批准 `mathjax` 包且 registry 显示当前包内已包含 `tex-svg.js`、`output/svg.js`、`adaptors/liteDOM.js` 等组件文件。
- Node helper 可参考官方组件 API：设置 `global.MathJax.loader`，加载 `tex-svg.js`，等待 `MathJax.startup.promise`，调用 `MathJax.tex2svgPromise(tex, {display, em, ex, containerWidth})`，再用 `MathJax.startup.adaptor.serializeXML(...)` 取 SVG。
- 生成 standalone SVG 时应配置 `svg.fontCache` 为 `local` 或 `none`，并将必要 CSS 写进 SVG；这样 `QTextDocument` 中每个公式资源更独立。
- SVG 颜色应在 helper 或 C++ 后处理阶段调成当前暗色主题可见的浅色，例如正文 `#e8eaed` 或略亮数学色；MathJax docs 的 standalone 示例里会设置 `g` 的 `stroke`/`fill`。

### External References

- npm registry current check: `npm view mathjax version license unpackedSize dist.unpackedSize dist.tarball --json`
  - Result: `mathjax@4.1.3`, license `Apache-2.0`, `dist.unpackedSize` `19971291`, tarball `https://registry.npmjs.org/mathjax/-/mathjax-4.1.3.tgz`.
- npm registry comparison: `npm view @mathjax/src version license unpackedSize dist.unpackedSize dist.tarball --json`
  - Result: `@mathjax/src@4.1.3`, license `Apache-2.0`, `dist.unpackedSize` `33726950`.
- npm package structure check: `npm pack mathjax@4.1.3 --dry-run --json`
  - The `mathjax` package contains component files including `tex-svg.js`, `tex-svg-nofont.js`, `output/svg.js`, `startup.js`, `adaptors/liteDOM.js`, `loader.js`, `input/tex.js`, and `LICENSE`.
- Local tool versions checked without installing dependencies:
  - `npm --version` -> `10.8.2`
  - `node --version` -> `v20.19.4`
- MathJax official docs:
  - Node components: https://docs.mathjax.org/en/v4.0/server/components.html
  - Direct Node API: https://docs.mathjax.org/en/v4.0/server/direct.html
  - Standalone SVG guidance: https://docs.mathjax.org/en/v4.0/web/convert.html#creating-stand-alone-svg-images
- Qt official docs:
  - `QTextDocument::addResource`: https://doc.qt.io/qt-6/qtextdocument.html#addResource
  - Qt SVG module and CMake usage: https://doc.qt.io/qt-6/qtsvg-index.html

### Related Specs

- Project WebView boundary: `AGENTS.md` says main editor, main preview, and AI Markdown rendering must not depend on WebView.
- Project dependency boundary: `AGENTS.md` says Qt, MD4C, Scintilla, QScintilla, or other third-party dependencies need separate dependency note and explicit approval.
- Current task old requirements conflict: `.trellis/tasks/08-21-native_markdown_renderer/prd.md:18` forbids Node/JavaScript runtime, and `.trellis/tasks/08-21-native_markdown_renderer/prd.md:21` says math remains styled source. User latest approval supersedes these for MathJax, so PRD/README should be updated before/with implementation.

### Files Likely Needing Modification

- `package.json` and `package-lock.json`: add `mathjax` dependency via `npm install mathjax --save`.
- `tools/mathjax-renderer.mjs` or similar new helper: perform TeX -> standalone SVG conversion.
- `src/gui/mathjax_svg_renderer.h/.cpp` or equivalent: C++ adapter around `QProcess`, cache, timeout, fallback, SVG -> image resource.
- `src/gui/markdown_document_renderer.h/.cpp`: pass/use math renderer and replace math source insertion with image insertion on success.
- `src/gui/main_window.cpp`: likely only if the renderer needs process lifetime ownership or theme/layout dimensions from the preview widget.
- `CMakeLists.txt`: add new C++ files; likely add `Qt6 COMPONENTS Widgets Svg` and link `Qt6::Svg` if SVG is rasterized in C++.
- `tests/markdown_document_renderer_tests.cpp`: update math fallback/source assertions and add success/failure tests; consider a fake math renderer interface to avoid requiring Node in every unit test.
- `README.md` and current Trellis task PRD/design: update math rendering scope, dependency, install/build notes, and residual risk.

## Caveats / Not Found

- No dependency was installed and no code was changed outside this research file.
- The current task already has dirty files in CMake, README, resources, GUI, tests, and Trellis task docs; implementation must preserve those changes.
- Direct `QTextDocument` SVG byte insertion was not runtime-verified. The safer route is `QSvgRenderer`/`Qt6::Svg` -> `QImage` -> `QTextDocument::addResource`.
- `mathjax` package path differs from official examples that use `@mathjax/src/bundle/...`; implementation should verify actual import path after `npm install mathjax --save`. Package exports show wildcard exports and package files include top-level `tex-svg.js`, so `import 'mathjax/tex-svg.js'` or package-relative resolved import is likely, but this must be tested after install.
- MathJax v4 can start speech worker threads; Node helpers should call `MathJax.done()` on shutdown, or keep a long-lived process with explicit termination.
- Launching Node synchronously from the UI thread can visibly stall preview. Batch conversion, cache, timeout, and fallback are necessary for large/windowed preview behavior.
- MathJax SVG output may contain CSS/font-cache dependencies; use standalone SVG settings and inject CSS/fill/stroke for dark theme.
- Code block visibility issue is separate from MathJax but in the same renderer. Increasing `renderCode()` top margin and using a lighter/different code block background or border would address the screenshot symptom; QTextDocument block formats do not provide a full CSS-like rounded card border.
