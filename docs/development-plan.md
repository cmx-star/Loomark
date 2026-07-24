# Loomark Development Plan

## Product Direction

Loomark is a fast, local-first Markdown editor built as a native desktop application, not a browser wrapper. It keeps Markdown source as the single source of truth so that opening and saving an unchanged document does not create Git noise.

The baseline stack is Vue 3 + TypeScript + Tauri 2 + CodeMirror 6. The project is licensed under Apache-2.0. Public projects including Marko, MarkText/Muya, DOMD, Flux Markdown, Tinta, and Typora are design references only; their code, licenses, and assets must be evaluated individually before reuse.

## External Reference Provenance

Planned WYSIWYG, Git, file-watching, export, and plugin features must be implemented independently. External projects may be used to understand user-visible behavior, workflows, or performance characteristics, but their source, assets, text, and tests must not be copied into Loomark by default.

Before importing any external code, asset, or substantial text, record the source URL, revision, file paths, license, and intended use in the pull request. The change must retain all required notices, have an explicit compatibility decision, and receive review before it is merged. A package manifest claim alone is insufficient when the repository license differs; use the repository license or obtain written clarification from the copyright holder.

## Capability Roadmap

| Milestone | Scope | Acceptance signal |
| --- | --- | --- |
| M0: performance proof | Native file access, CodeMirror source editor, file preflight, 1/10 MiB full loading, 50 MiB progressive loading, metrics, no-edit round trip, system print entry | Repeatable fixture and automated boundary/round-trip tests |
| M1: editing foundation | Tabs, source/reading/split modes, session restore, themes, watcher/reload conflict UI | A reopened session restores documents and mode without overwriting external changes |
| M1.1: desktop application shell | Collapsible directory navigator, focused document workspace, native command menus, compact status bar, localized application chrome | The primary window keeps navigation separate from document rendering and remains usable in Chinese and English |
| M2: document workflow | Git status/diff, save-as, HTML/PDF/DOCX export, Markdown profiles and typography settings | Exported output matches selected theme and Git changes are inspectable |
| M3: extensibility | Declarative plugins, permission manifest, isolated worker/iframe execution, signed/reviewed catalog option | Plugins cannot access filesystem/network outside granted permissions |
| M4: AI writing | Cloud providers first, OpenAI-compatible local endpoint, streaming edits and context bounds | Edits are reviewable and no large document is silently sent in full |

## Large-File Policy

| File size | Default path | Explicitly not promised by default |
| --- | --- | --- |
| Up to 10 MiB | Read full source, initialize CodeMirror, permit editing | None beyond normal machine limits |
| Over 10 MiB to 50 MiB | Preflight metadata, show bounded first chunk, load source in background; source mode only until ready | Immediate full WYSIWYG, split view, full-document diff, full AI context |
| Over 50 MiB | Warn and require a deliberate future policy | Supported editing or rendering |

The implementation must preserve the original bytes for an unchanged document. Text normalization, AST serialization, automatic formatter passes, and background encoding conversion cannot run on the default open-save path.

## Export Plan

- HTML: render with the selected application theme and write a self-contained export artifact.
- PDF: invoke the operating-system print dialog so users can select a printer or Save as PDF.
- DOCX: invoke a fixed-version Pandoc sidecar shipped with the application. Validate binary availability and return actionable diagnostics when unavailable.

## Security and Plugin Model

The main WebView is trusted application code only. Initial plugins are declarative theme, command, syntax, and export descriptors. Any later executable plugin runs in an isolated worker or iframe with a versioned manifest, explicit filesystem/network/clipboard permissions, timeout and memory controls, and a user-visible publisher/permission review. Installing arbitrary executable code is therefore a later opt-in, not an M0 assumption.

## AI Plan

Cloud providers are first. Provider adapters use a redacted settings store and streaming requests; the user selects context scope before each operation. OpenAI-compatible endpoints provide the initial local-model bridge after cloud support. The default context limit must remain below the progressive large-file threshold and never auto-upload a complete large document.

## M0 Verification Plan

1. Generate deterministic Markdown fixtures of 1, 10, and 50 MiB.
2. Confirm 1 and 10 MiB choose the full editor path.
3. Confirm 50 MiB chooses the progressive path and returns a bounded first chunk.
4. Record byte size, line count, longest line, read time, and editor initialization time.
5. Verify a no-edit round trip is byte-exact; future persistence tests may compare SHA-256 hashes without changing the source bytes.
6. Run TypeScript checks, Vitest, Vite production build, Rust `cargo check`, and a browser-visible development build check.

## Risks and Mitigations

| Risk | Mitigation | M0 status |
| --- | --- | --- |
| WebView/editor memory pressure on large source | Size classification, bounded first chunk, delayed full source allocation | In scope |
| Markdown mode conversion creates unwanted Git diffs | Source bytes are canonical; no AST reserialization on save | In scope |
| File changes outside the app race with edits | Watch only opened documents; compare content after an external save and require an explicit reload or keep decision only when it differs | Implemented; native-window verification remains |
| Pandoc licensing, binary size, and platform compatibility | Pin a tested sidecar per platform and expose diagnostics | Contract only |
| Plugin arbitrary code compromises local files | Declarative first; permissioned isolation before execution | Designed, not implemented |
| AI leaks content or cost surprises | Explicit provider, context selection, redaction, token bounds | Designed, not implemented |
| Reference implementation or asset introduces an incompatible license or attribution obligation | Independent implementation by default; record provenance and review any import before merge | Applies to future WYSIWYG, Git, watcher, export, and plugin work |

## M0 Non-Goals

AI integration, Git UI, executable plugins, a Pandoc download, production visual polish, session restore, and cross-platform release packaging are intentionally excluded from the validation prototype.

## M0 Validation Evidence (2026-07-24)

- Passed: TypeScript check, Vitest (4 tests), Vite production build, and deterministic 1/10/50 MiB fixture generation.
- Passed: code splitting reduces the initial application JavaScript bundle to 70.13 kB; the 502.67 kB CodeMirror chunk loads only with the source editor.
- Blocked by the execution sandbox: `cargo check` cannot resolve `static.crates.io`, and Vite cannot bind `127.0.0.1:1420` for browser-visible verification. These are environment limits, not passing desktop checks.
- Still required outside this sandbox: `cargo check`, native Tauri file-dialog and progressive-loading exercise using the 50 MiB fixture, system print dialog verification, and screenshot-based desktop/mobile layout verification.

## M1 Foundation (2026-07-24)

This slice makes the application usable as a local Markdown editor: multiple tabs, dirty indicators, explicit saves, source/reading/split modes, safe Markdown rendering, two built-in themes, and local session restoration. The renderer uses `markdown-it` with raw source HTML disabled; only renderer-created HTML is inserted into the reading surface.

Unchanged documents never invoke the save command, preserving the M0 no-edit byte-preservation rule. Changed documents are saved only through an explicit Save action or Cmd/Ctrl+S.

Opened documents are watched through the platform file-event backend. After an external save, Loomark reads and compares the disk content with its in-memory buffer. A matching save is ignored. A difference creates a non-destructive prompt regardless of document size or local dirty state: reload replaces the in-memory document only after the user chooses it, while keep leaves the current buffer intact until a later explicit save. Git, exports, plugins, and AI remain later milestones.

## Internationalization Baseline (2026-07-24)

The default application locale is `zh-CN`, with `en-US` as the initial alternate locale and fallback. Visible interface strings use centralized message keys and the user's selected locale is persisted separately from document sessions. Future locales must add messages rather than embedding text in components; document content is never translated automatically.

### Internationalization Validation Evidence

- Passed: locale fallback and explicit English selection tests; TypeScript, Vitest (11 tests), Vite build, and `cargo check`.
- Bundle impact: the initial JavaScript bundle is 239.24 kB (96.35 kB gzip). CodeMirror remains an on-demand editor chunk.

## M1.1 Desktop Application Shell (planned)

The primary desktop window must present a restrained application shell rather than a page-level command toolbar. The left side contains only a collapsible directory and document navigator. It may show the active workspace tree, open documents, and file-state affordances, but it must not become a second editor, preview, inspector, or metrics panel.

The right side is the document workspace. It owns document tabs and the selected source, reading, or split mode. Reading and split surfaces render Markdown in this workspace; source editing remains available through CodeMirror. The collapsed navigator leaves the workspace usable for focused reading and editing.

File, edit, view, appearance, language, and window commands belong in native Tauri menus where the platform supports them. The application surface may retain only contextual controls that cannot be expressed naturally in a native menu. The existing page-level command toolbar is therefore a transitional implementation, not the target shell.

File size, line count, loading state, mode, and save state move to a low-interference bottom status bar. This preserves large-file observability without competing with the document workspace.

Internationalization is a cross-cutting requirement for the shell: `zh-CN` remains the default and `en-US` the first alternate locale; native menu labels, navigator labels, tab/state text, status-bar text, dialogs, and mode/theme controls must use centralized message keys. Locale selection persists separately from workspace-session state. Long localized labels must not overflow, resize the fixed navigation or status areas, or obscure document content.

### M1.1 Acceptance Criteria

1. The navigator can be collapsed and restored without losing the active document, tab order, document mode, or unsaved-state indication.
2. The main workspace has a stable source, reading, and split presentation at desktop sizes; split mode keeps source and rendered content independently scrollable.
3. Open, save, print, tab, view-mode, theme, and locale commands are reachable through native menus, with application-surface controls limited to document-contextual actions.
4. The bottom status bar exposes loading and performance information without adding a right-side utility panel.
5. `zh-CN` and `en-US` render all application-chrome strings from centralized catalogs, persist the chosen locale, and preserve the layout under the longest supported labels.
6. Native-menu behavior, collapsible navigation, mode switching, theme switching, and locale switching receive a user-visible Tauri verification on supported desktop platforms.

### M1.1 Implementation Evidence (2026-07-24)

- Implemented: a persisted collapsible left navigator with tabs for open documents and the current directory, defaulting to the active document's directory, a right-side tabbed document workspace, source/reading/split surfaces, and a compact bottom status bar. The directory section reads only the current level on demand, shows direct child directories plus Markdown files, provides an explicit parent-directory action, caps output at 1,000 entries, and opens files through the existing loading policy. A recursive filesystem tree remains deferred until it has bounded scanning and large-directory virtualization rules.
- Implemented: File, Edit, View, Appearance, Language, and Window menus are built with the Tauri menu API. All custom titles and labels resolve through the `zh-CN`/`en-US` catalogs, and language, theme, mode, selected-document, dirty, and navigation state rebuild the menu so checked and enabled states remain current.
- Passed: TypeScript, Vitest (11 tests), Vite production build, `cargo check`, and browser-visible checks for the empty workspace plus navigator collapse/expand.
- Still required: a user-visible Tauri-window check of operating-system menu rendering, command dispatch, and a loaded document in source/reading/split modes. Browser previews deliberately do not create native application menus.

### M1 Validation Evidence

- Passed: TypeScript check, Vitest (17 tests), Vite production build, `cargo check`, and Rust unit tests.
- Still required: a user-visible Tauri window check that changes an opened file externally, verifies reload and keep outcomes, and exercises native file dialog, save, session restore, and print interactions.
