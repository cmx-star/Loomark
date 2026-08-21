<!-- TRELLIS:START -->
# Trellis Instructions

These instructions are for AI assistants working in this project.

This project is managed by Trellis. The working knowledge you need lives under `.trellis/`:

- `.trellis/workflow.md` — development phases, when to create tasks, skill routing
- `.trellis/spec/` — package- and layer-scoped coding guidelines (read before writing code in a given layer)
- `.trellis/workspace/` — per-developer journals and session traces
- `.trellis/tasks/` — active and archived tasks (PRDs, research, jsonl context)

If a Trellis command is available on your platform (e.g. `/trellis:finish-work`, `/trellis:continue`), prefer it over manual steps. Not every platform exposes every command.

If you're using Codex or another agent-capable tool, additional project-scoped helpers may live in:
- `.agents/skills/` — reusable Trellis skills
- `.codex/agents/` — optional custom subagents

Managed by Trellis. Edits outside this block are preserved; edits inside may be overwritten by a future `trellis update`.

<!-- TRELLIS:END -->

## Project Rules

- Default implementation language for project work is Chinese for user-facing status and documentation notes unless a file is already English-only.
- The P0 target is macOS-first large-file Markdown editing infrastructure: large-file open, byte-range reading, safe save paths, streaming preview indexing, and later native editor integration.
- Do not introduce Qt, MD4C, Scintilla, QScintilla, or other third-party dependencies without a separate dependency note and explicit user approval.
- QScintilla is not allowed by default because its GPLv3/commercial licensing decision is unresolved. Prefer direct Scintilla source integration only after approval.
- Main editor, main preview, and AI Markdown rendering must not depend on WebView.
- Keep UI and background work separated: background code must not directly touch future Scintilla or QML objects.
- Large generated samples belong under `samples/generated/` and must not be committed.
- Until CMake is installed, verify the standard C++ core with direct `clang++` commands from `README.md`.
