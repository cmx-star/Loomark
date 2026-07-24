# Contributing to Loomark

## Before you start

- Search existing issues and pull requests before reporting a duplicate.
- Keep Markdown source bytes canonical: do not add implicit formatting, newline normalization, or AST reserialization to the no-edit open/save path.
- Preserve the 10 MiB full-editor threshold and the 50 MiB supported limit unless the change includes an approved performance policy update.
- Do not introduce executable third-party plugins into the main WebView.

## Development setup

Use Node.js 20 LTS or newer, pnpm, and the platform prerequisites for Electron 41.

```bash
pnpm install --frozen-lockfile
pnpm electron:dev
```

Before opening a pull request, run:

```bash
pnpm typecheck
pnpm test:run
pnpm build
pnpm electron:package
```

## Pull requests

- Keep each pull request focused on one user-visible change or defect.
- Explain the behavior change, test coverage, and platform limitations.
- Add or update behavior-focused tests when changing loading, saving, sessions, directory browsing, or localized UI behavior.
- Do not commit generated `dist`, `node_modules`, `.vite`, or `out` content.

By submitting a contribution, you agree to license it under Apache-2.0.
