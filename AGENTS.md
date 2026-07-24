# Project Instructions

## Product

- Loomark is an open-source, local-first Markdown desktop editor.
- The technical baseline is Vue 3, TypeScript, Tauri 2, and CodeMirror 6.
- Markdown source is the canonical document state. Opening and saving without edits must preserve bytes whenever the filesystem allows it.

## Large Files

- The initial supported target is Markdown files up to 50 MiB.
- Files at or below 10 MiB may use the full editor path.
- Files above 10 MiB must use a progressive path: preflight metadata first, a bounded first chunk for immediate display, then background source loading. Do not make a full WYSIWYG render, full diff, or full AI context the default for this tier.
- Measure and surface file size, line count, longest line, read time, and editor initialization time in performance prototypes.

## Platform Contracts

- PDF export opens the operating-system print dialog; do not embed a browser-only PDF conversion fallback as the primary path.
- DOCX export will use a version-pinned Pandoc sidecar distributed with the app. During M0 only provide discovery/path contracts; do not download a binary.
- Plugins must not run arbitrary third-party code in the main WebView. Begin with declarative plugins and define explicit permissions before isolated execution is introduced.

## Verification

- Run the smallest relevant tests first, then `pnpm typecheck`, `pnpm test:run`, `pnpm build`, and `cargo check --manifest-path src-tauri/Cargo.toml` for changes that affect the M0 prototype.
- Keep tests behavior-focused. For file loading, cover classification boundaries and byte-exact no-edit round trips.
