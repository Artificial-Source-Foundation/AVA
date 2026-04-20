---
title: "How-to: Download AVA Desktop"
description: "Download AVA Desktop from GitHub Releases when a version includes desktop bundles, or build it locally when it does not."
order: 2
updated: "2026-04-20"
---

# How-to: Download AVA Desktop

Use this page when you want the desktop app, not the terminal-first `ava` CLI.

AVA Desktop is the Tauri app in this repository. It is a separate product surface from the `ava` CLI.

See also: [How-to: Install AVA](install.md), [Reference: Install and release paths](../reference/install-and-release-paths.md)

## Choose Your Desktop Path

| I want to... | Use this path | Notes |
|---|---|---|
| Download a published desktop build | [Fast path](#fast-path) | Best option when the release includes your platform bundle |
| Build the desktop app from a repo checkout | [If the release does not include a desktop download](#if-the-release-does-not-include-a-desktop-download) | Supported fallback today |
| Run the desktop app from source with explicit commands | [Build the desktop app manually from source](#build-the-desktop-app-manually-from-source) | Best for local development |

## Fast path

1. Open <https://github.com/Artificial-Source/AVA/releases>
2. Pick the release version you want
3. Download the desktop bundle for your platform if that release includes one

Current desktop bundle types are:

1. Linux: `.deb`, `.AppImage`, `.rpm`
2. macOS: `.dmg`, `.app`
3. Windows: `.msi`, `.exe`

Not every release includes every desktop bundle.

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

That is the normal fallback path today.

## Build the desktop app manually from source

If you want the lower-level flow instead of the guided installer:

1. Install JavaScript dependencies
2. Run the Tauri app from the local checkout

```bash
pnpm install --reporter=silent
pnpm tauri dev
```

If you only want the terminal app, stop here and use [How-to: Install AVA](install.md) instead. The desktop path uses the frontend workspace under `src/` and the Tauri app under `src-tauri/`.

## Current release availability note

Desktop downloads are not guaranteed on every release.

The practical rule is simple: check the GitHub Releases page for the version you want. If your platform bundle is not there, build from source.
