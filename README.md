# markdown-qt

P0 prototype for a native large-file Markdown editor. The current repository starts with the macOS-first core path: classify large files, inspect basic file metadata, read byte ranges, build a lightweight streaming preview index, and replace files through a sibling temporary file.

The product roadmap is in `large-markdown-editor-technical-solution-and-roadmap.md`, which is kept outside Git by project `.gitignore` until the plan is explicitly promoted into tracked documentation.

## Current scope

- Standard C++20 core library.
- CLI entry point for local inspection.
- No Qt, MD4C, or Scintilla dependency is linked yet.
- No WebView or HTML preview path.
- Byte-range locate and literal search are available in the CLI prototype.
- Search and locate report 1-based byte columns; a UTF-8 BOM at file start is treated as zero-width.

## Build after CMake is installed

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
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
