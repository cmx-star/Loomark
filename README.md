# markdown-qt

P0 prototype for a native large-file Markdown editor. The current repository now includes a cross-platform基础 GUI首版: open a local Markdown file, edit it, preview it in the same window, and save it back through the core atomic write path. The first build and verification environment is macOS.

The product roadmap is in `large-markdown-editor-technical-solution-and-roadmap.md`, which is kept outside Git by project `.gitignore` until the plan is explicitly promoted into tracked documentation.

## Current scope

- Standard C++20 core library.
- CLI entry point for local inspection.
- Qt Widgets desktop GUI entry point.
- PyQtDarkTheme-derived dark Qt stylesheet is embedded as a Qt resource for cross-platform widget styling.
- No WebView or HTML preview path.
- Byte-range locate and literal search are available in the CLI prototype.
- Search and locate report 1-based byte columns; a UTF-8 BOM at file start is treated as zero-width.

## Build after CMake is installed

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
open ./build/macos-debug/markdown_qt_gui.app
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
