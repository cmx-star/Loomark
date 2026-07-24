# Loomark

[简体中文](README.md)

Loomark is an open-source, local-first Markdown desktop editor built with Vue 3, Tauri 2, and CodeMirror 6. It runs as a native desktop application, not a web-app wrapper, and keeps Markdown source as the canonical document state.

The default interface language is Simplified Chinese. English is currently the first alternate locale.

## Current Capabilities

- Open, edit, explicitly save, and reopen local Markdown files.
- Source, reading, and split modes with independently scrollable source and preview surfaces.
- Tabs, unsaved-change indication, local session restoration, paper/night themes, and a collapsible workspace navigator.
- Native application menus for file, edit, view, appearance, language, and window commands.
- Current-folder navigation that reads one directory level at a time and shows Markdown files plus direct child folders.
- Watch open documents for external saves. Loomark compares the on-disk content after a save: unchanged content is ignored, while changed content prompts the user to reload the version on disk or keep current edits. Loomark never replaces editor content automatically.
- Markdown preflight metrics for file size, line count, longest line, read time, and editor initialization time.

## Large-File Policy

Loomark currently targets Markdown files up to 50 MiB:

| File Size | Default Behavior |
| --- | --- |
| Up to 10 MiB | Load the complete source into CodeMirror. |
| Over 10 MiB to 50 MiB | Run metadata preflight, show a bounded preview, then load source in the background. Full preview, diff, and AI context are not the default path. |
| Over 50 MiB | Outside the current supported range. |

Opening and saving an unchanged document does not invoke a save operation, preserving its bytes whenever the filesystem allows it.

## Development

Prerequisites:

- Node.js 20 LTS or newer
- pnpm 9 or newer
- Rust stable and the platform prerequisites required by Tauri 2

```bash
pnpm install --frozen-lockfile
pnpm tauri:dev
```

Useful checks:

```bash
pnpm typecheck
pnpm test:run
pnpm build
cargo check --manifest-path src-tauri/Cargo.toml
```

Create a distributable desktop bundle with:

```bash
pnpm tauri:build
```

## Roadmap

Planned work includes Git status/diff, Save As, HTML/PDF/DOCX export, declarative plugins with explicit permissions, and opt-in AI writing assistance. See [the development plan](docs/development-plan.md) for constraints and milestones.

## Privacy and Security

Loomark is local-first. Current document operations stay on the local machine. Future AI functionality will require explicit provider and context selection; it is not part of the current release surface.

Please report security issues through [GitHub Security Advisories](https://github.com/cmx-star/noteMD/security/advisories/new), not public issues. See [SECURITY.md](SECURITY.md).

## Identity Migration

The development prototype was renamed from Marko to Loomark. New desktop bundles use the Bundle Identifier `io.md.loomark`. Browser-local session and locale keys are migrated once when they remain accessible; operating systems treat a changed Bundle Identifier as a separate application identity, so keep the previous prototype installation until local documents have been checked in Loomark.

## Contributing

Contributions are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening an issue or pull request.

## License

Copyright 2026 Loomark contributors.

Licensed under the [Apache License 2.0](LICENSE).
