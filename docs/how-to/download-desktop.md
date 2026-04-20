---
title: "How-to: Download AVA Desktop"
description: "Download AVA Desktop from GitHub Releases when a version includes desktop bundles, or build it locally when it does not."
order: 2
updated: "2026-04-19"
---

# How-to: Download AVA Desktop

Use this page when you want the desktop app, not the terminal-first `ava` CLI.

AVA Desktop is the Tauri app in this repository. It is a separate install surface from the CLI/TUI binary.

See also: [How-to: Install AVA](install.md), [Reference: Install and release paths](../reference/install-and-release-paths.md), [Contributing: Releasing](../contributing/releasing.md)

## Fast path

1. Open <https://github.com/Artificial-Source/AVA/releases>
2. Pick the release version you want
3. Download the desktop bundle for your platform if that release includes one

Current desktop bundle types produced by the documented Tauri release flow are:

1. Linux: `.deb`, `.AppImage`, `.rpm`
2. macOS: `.dmg`, `.app`
3. Windows: `.msi`, `.exe`

Because desktop publishing is still a manual maintainer flow in this repo, not every release should be assumed to include every desktop bundle.

## Platform examples

When a release includes desktop artifacts, use the bundle type that best matches your platform:

1. macOS Apple Silicon or Intel: the `.dmg` download
2. Windows x64: the `.msi` or `.exe` installer
3. Debian/Ubuntu-based systems: `.deb`
4. Fedora/RHEL/openSUSE-style package workflows: `.rpm`
5. Portable Linux install: `.AppImage`

## If the release does not include a desktop download

Build the desktop app from a local checkout instead:

```bash
git clone https://github.com/Artificial-Source/AVA.git && cd AVA
./install-from-source.sh --desktop
```

That is the supported fallback path today.

## Build the desktop app manually from source

If you want the lower-level flow instead of the guided installer:

```bash
pnpm install --reporter=silent
pnpm tauri dev
```

For release-style local bundles, use the maintainer release workflow in [`../contributing/releasing.md`](../contributing/releasing.md).

Grounding: [`../../install-from-source.sh`](../../install-from-source.sh), [`../../package.json`](../../package.json), [`../../src-tauri/tauri.conf.json`](../../src-tauri/tauri.conf.json)

## Current release availability note

If you want predictable desktop downloads on every version, the repo is not fully there yet.

Today, the documented gaps are:

1. Desktop publishing is still manual, not part of the tag-driven automated release workflow
2. The current desktop release doc shows a sample `gh release create` command that uploads Linux artifacts only, not a complete cross-platform desktop bundle set
3. Cross-platform desktop CI/build/sign/upload automation is not wired into `.github/workflows/release.yml`
4. CI secrets/signing setup for automated desktop publishing is documented as future work, not current behavior

That means the GitHub Releases page is the real source of truth for whether a given version includes desktop downloads.
