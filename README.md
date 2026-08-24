# markdown-qt

P0 prototype for a native large-file Markdown editor. The current repository now includes a cross-platform基础 GUI首版: open a local Markdown file, edit it, preview it in the same window, and save it back through the core atomic write path. The first build and verification environment is macOS.

The product roadmap is in `large-markdown-editor-technical-solution-and-roadmap.md`, which is kept outside Git by project `.gitignore` until the plan is explicitly promoted into tracked documentation.

## Current scope

- Standard C++20 core library.
- CLI entry point for local inspection.
- Qt Widgets desktop GUI entry point.
- PyQtDarkTheme-derived dark Qt stylesheet is embedded as a Qt resource for cross-platform widget styling.
- Native Markdown preview parses with `md4qt` and renders directly into `QTextDocument`; it does not use WebView or an HTML preview path.
- Byte-range locate and literal search are available in the CLI prototype.
- Search and locate report 1-based byte columns; a UTF-8 BOM at file start is treated as zero-width.

## Markdown preview

- Parser: KDE `md4qt` 5.1.3, pinned to commit `1a878810adeeb534de4de395f4951edd54ab6072` under the MIT license; macOS bundles copy the license to `Contents/Resources/licenses/md4qt-MIT.txt`.
- Renderer: project-owned AST-to-`QTextDocument` rendering for headings, inline styles, code, quotes, lists, links, images, tables, and thematic breaks.
- Safety: raw HTML is displayed as text, remote images are not downloaded, and unsafe link schemes are not made clickable.
- Large documents retain the existing windowed preview path instead of parsing the full editor buffer on every refresh.
- Math expressions use MathJax 4.1.3 to convert TeX to SVG, then QtSvg rasterizes the SVG into `QTextDocument` image resources. If Node.js, the helper script, or MathJax is unavailable, formulas fall back to visible styled source.
- Mermaid diagram rendering is deferred. WebEngine, Marked.js, and QWebChannel are not part of the current preview path.
- The GUI writes a local `loomark.log` under the operating system app data location. Users can open the directory from `帮助 -> 打开日志目录` when reporting startup, rendering, or update-check issues.
- On startup, and from `帮助 -> 检查更新`, the GUI checks the latest GitHub Release at `https://github.com/cmx-star/Loomark/releases`. If a newer version exists, it opens the platform release asset download or the release page after user confirmation.

`md4qt` is acquired with CMake `FetchContent`. The first configure needs network access to the official KDE repository; subsequent builds can reuse the populated `build/<preset>/_deps` cache. MathJax is acquired with npm; packaging copies only the trimmed MathJax files needed for TeX-to-SVG preview. macOS and Windows packages also include the Node.js executable used by the helper so formulas render when the app is launched outside a developer shell. The Linux `.deb` declares `nodejs` as a package dependency.

## Build after CMake is installed

```bash
npm install
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
open ./build/macos-debug/Loomark.app
```

## GitHub packaging

GitHub Actions now builds macOS, Linux, and Windows artifacts on push, pull request, and manual dispatch. Each run uploads native platform packages:

- macOS: `loomark-macos.dmg`
- Linux: `loomark-linux.deb`
- Windows: `loomark-windows-setup.exe`

The macOS DMG includes `打开前请读.txt`. Until the app is signed with an Apple Developer ID and notarized, some downloaded builds may need:

```bash
xattr -dr com.apple.quarantine /Applications/Loomark.app
```

Pushing a version tag creates or updates a GitHub Release with those three packages:

```bash
git tag v0.1.0
git push origin v0.1.0
```

## Temporary verification without CMake

Apple Command Line Tools can compile the current standard-library-only slice directly:

```bash
clang++ -std=c++20 -Wall -Wextra -Wpedantic -Isrc \
  src/core/document_file.cpp src/core/file_tier.cpp src/core/markdown_index.cpp \
  tests/core_tests.cpp -o /tmp/markdown_qt_core_tests
/tmp/markdown_qt_core_tests
```

## CLI examples

```bash
./build/macos-debug/markdown_qt_p0 inspect path/to/file.md
./build/macos-debug/markdown_qt_p0 index path/to/file.md 200
./build/macos-debug/markdown_qt_p0 search path/to/file.md needle 50
./build/macos-debug/markdown_qt_p0 locate path/to/file.md 120 180
./build/macos-debug/markdown_qt_p0 chunk path/to/file.md 0 4096
```
