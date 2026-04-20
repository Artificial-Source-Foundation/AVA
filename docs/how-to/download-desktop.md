---
title: "How-to: Download AVA Desktop"
description: "Download AVA Desktop from GitHub Releases when a version includes desktop bundles, or build it locally when it does not."
order: 2
updated: "2026-04-20"
---

# How-to: Download AVA Desktop

Use this page when you want the desktop app, not the terminal-first `ava` CLI.

AVA Desktop is the Tauri app in this repository. It is a separate product surface from the `ava` CLI.

See also: [How-to: Install AVA](install.md), [Reference: Install and release paths](../reference/install-and-release-paths.md), [Contributing: Releasing](../contributing/releasing.md)

## Choose Your Desktop Path

| I want to... | Use this path | Notes |
|---|---|---|
| Download a published desktop build | [Fast path](#fast-path) | Best option when the release includes your platform bundle |
| Build the desktop app from a repo checkout | [If the release does not include a desktop download](#if-the-release-does-not-include-a-desktop-download) | Supported fallback today |
| Run the desktop app from source with explicit commands | [Build the desktop app manually from source](#build-the-desktop-app-manually-from-source) | Best for contributors and power users |

## Fast path

1. Open <https://github.com/Artificial-Source/AVA/releases>
2. Pick the release version you want
3. Download the desktop bundle for your platform if that release includes one

Current desktop bundle types produced by the documented Tauri release flow are:

1. Linux: `.deb`, `.AppImage`, `.rpm`
2. macOS: `.dmg`, `.app`
3. Windows: `.msi`, `.exe`

Because desktop publishing is still a manual maintainer flow in this repo, not every release should be assumed to include every desktop bundle.

If the release page does not include the desktop bundle you need, use the source-build fallback below.

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

This path checks dependencies and routes through the repo's desktop source-build helper.

## Build the desktop app manually from source

If you want the lower-level flow instead of the guided installer:

1. Install JavaScript dependencies
2. Run the Tauri app from the local checkout

```bash
pnpm install --reporter=silent
pnpm tauri dev
```

For release-style local bundles, use the maintainer release workflow in [`../contributing/releasing.md`](../contributing/releasing.md).

If you only want the terminal app, stop here and use [How-to: Install AVA](install.md) instead. The desktop path uses the frontend workspace under `src/` and the Tauri app under `src-tauri/`.

Grounding: [`../../install-from-source.sh`](../../install-from-source.sh), [`../../package.json`](../../package.json), [`../../src-tauri/tauri.conf.json`](../../src-tauri/tauri.conf.json)

## Current release availability note

If you want predictable desktop downloads on every version, the repo is not fully there yet.

Today, the documented gaps are:

1. Desktop publishing is still manual, not part of the tag-driven automated release workflow
2. The current desktop release doc shows a sample `gh release create` command that uploads Linux artifacts only, not a complete cross-platform desktop bundle set
3. Cross-platform desktop CI/build/sign/upload automation is not wired into `.github/workflows/release.yml`
4. CI secrets/signing setup for automated desktop publishing is documented as future work, not current behavior

That means the GitHub Releases page is the real source of truth for whether a given version includes desktop downloads.
