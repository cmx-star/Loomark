# Changelog

All notable changes to Loomark are documented in this file.

## [Unreleased]

## [0.0.2] - 2026-07-24

### Fixed

- Fixed cross-platform Electron Forge maker loading under pnpm and added the package metadata required for Debian bundles.

### Added

- Open-source repository documentation, security policy, contribution guidance, and GitHub automation.
- A release SBOM workflow that produces an SPDX artifact for dependency review.

### Changed

- Renamed the application, package, benchmark fixtures, and desktop identity from Marko to Loomark.
- Changed the Bundle Identifier to `io.md.loomark` and migrated accessible browser-local session and locale keys on first launch.

## [0.0.1] - 2026-07-24

### Added

- Local Markdown opening, source editing, explicit saving, and large-file loading policy through 50 MiB.
- Source, reading, and split modes; themes; tabs; session restoration; localized Chinese and English application chrome.
- Current-directory navigator and native desktop command menus.
