# P0 dependency plan

Status: documented only. No dependency has been installed by this task.

## Approved implementation boundary

The current P0 code is intentionally limited to standard C++20 so it can be compiled with Apple Command Line Tools before Qt, MD4C, or Scintilla are installed. This gives the project an early verification path for file tiering, byte-range reads, safe replacement writes, and streaming preview indexing.

## Homebrew packages for macOS

Install command, when approved:

```bash
brew install cmake qt md4c
```

### cmake

- Purpose: configure and build the C++20 project using `CMakeLists.txt` and `CMakePresets.json`.
- Evidence: `brew info cmake` reported stable `4.4.2`, not installed on this machine.
- License: BSD-3-Clause according to Homebrew metadata.
- Risk: local builds cannot use the CMake presets until installed.
- Alternative: invoke `clang++` directly for the standard-library-only core prototype.

### qt

- Purpose: future native macOS desktop shell and QML UI.
- Evidence: `brew info qt` reported stable `6.11.1`, alias `qt6`, not installed on this machine.
- License note: Homebrew metadata lists multiple Qt module licenses, including LGPL/GPL terms. The app needs a separate distribution compliance review before release packaging.
- Risk: large install size, many Qt modules, packaging and license obligations.
- Alternative: delay GUI work and keep validating core behavior through CLI and tests.

### md4c

- Purpose: future event-style Markdown parsing backend after the streaming index prototype proves file handling.
- Evidence: `brew info md4c` reported stable `0.5.3`, not installed on this machine.
- License: MIT according to Homebrew metadata.
- Risk: integration still needs cross-block semantic tests; the current prototype does not depend on it.
- Alternative: keep the local streaming block detector for P0 indexing until MD4C integration is approved.

## Scintilla decision

Homebrew does not provide a direct `scintilla` formula on this machine. It suggested `qscintilla2`, but QScintilla remains excluded from P0 because the technical plan flags its GPLv3/commercial licensing as a separate product decision.

Proposed future route, after approval:

```bash
git submodule add https://github.com/ScintillaOrg/scintilla.git third_party/scintilla
```

- Purpose: direct native Scintilla integration so the desktop editor can keep Scintilla Document as the text source of truth.
- Risk: Qt integration work is non-trivial; upstream source layout and supported build path must be verified.
- Alternative: revisit QScintilla only after final project license and commercial/GPL policy are decided.
