---
title: "How-to: Install AVA"
description: "Install AVA through the fastest binary path, a manual release download, or a more explicit source build depending on how much control you want."
order: 1
updated: "2026-04-19"
---

# How-to: Install AVA

Use this page when you want the shortest path to a working AVA install without guessing which route is meant for you.

AVA has two install surfaces:

1. `ava` CLI/TUI for terminal and headless use
2. AVA Desktop for the Tauri app

See also: [Tutorial: First run](../tutorials/first-run.md), [How-to: Download AVA Desktop](download-desktop.md), [How-to: Run AVA locally](run-locally.md), [Reference: Install and release paths](../reference/install-and-release-paths.md)

## Choose your path

| If you want... | Use this path |
|---|---|
| Fastest CLI install on Linux or macOS | `install.sh` |
| Windows install or a manual binary download | GitHub Releases |
| A repo-pinned CLI build or a custom feature build | `cargo install --path ...` |
| A guided source build for CLI and desktop from a checkout | `./install-from-source.sh` |

## Install AVA CLI

## Fast install from release binaries (Linux/macOS)

Run the repository installer script:

```bash
curl -fsSL https://raw.githubusercontent.com/Artificial-Source/AVA/develop/install.sh | sh
```

This is the best default for most CLI users because it does not require Rust, Node.js, or a repo checkout.

Current script behavior:

1. Detects Linux/macOS + `x86_64`/`aarch64`
2. Downloads the latest matching release archive from GitHub Releases
3. Installs `ava` to `~/.ava/bin` by default
4. Tries to add that path to common shell rc files
5. Verifies checksums when a `.sha256` asset is available

If you want a different install directory, override it explicitly:

```bash
curl -fsSL https://raw.githubusercontent.com/Artificial-Source/AVA/develop/install.sh | AVA_INSTALL_DIR=/usr/local/bin sh
```

If your shell has not picked up the PATH update yet, reload it or run:

```bash
export PATH="$HOME/.ava/bin:$PATH"
ava --version
```

Grounding: [`../../install.sh`](../../install.sh), [`../../dist-workspace.toml`](../../dist-workspace.toml)

Repo-slug note: release-related links in this checkout are aligned to `Artificial-Source/AVA`.

## Install from GitHub Releases (manual)

Use this path when you want to inspect the asset yourself, pin a specific release, or install on Windows.

1. Open <https://github.com/Artificial-Source/AVA/releases>
2. Pick the release version you want
3. Download the asset that matches your OS and architecture
4. Put the extracted `ava` binary on your `PATH`

Current CLI release targets are:

1. `aarch64-apple-darwin`
2. `x86_64-apple-darwin`
3. `aarch64-unknown-linux-gnu`
4. `x86_64-unknown-linux-gnu`
5. `x86_64-pc-windows-msvc`

The release pipeline is also configured to generate shell and PowerShell installers. `install.sh` is the Unix entrypoint in this repo; Windows users should use the GitHub Releases page and choose the Windows asset or generated installer for the version they want.

This route is also the fallback for platforms not handled by `install.sh`.

### Exact CLI asset examples

AVA's CLI release archives follow the target-triple naming used by `install.sh` and `cargo-dist`.

Examples:

1. macOS Apple Silicon: `ava-aarch64-apple-darwin.tar.gz`
2. macOS Intel: `ava-x86_64-apple-darwin.tar.gz`
3. Linux ARM64: `ava-aarch64-unknown-linux-gnu.tar.gz`
4. Linux x64: `ava-x86_64-unknown-linux-gnu.tar.gz`
5. Windows x64: `ava-x86_64-pc-windows-msvc.zip` or the generated Windows installer on the release page

If you are scanning a release page quickly, these names are the fastest way to map OS and architecture to the right CLI download.

## Install the CLI from source

Use this when you want the build to come from your checked-out source, you need a feature-gated build, or you prefer Cargo's install flow over downloading release assets.

From the repository root:

```bash
cargo install --path crates/ava-tui --bin ava
```

This remains the clearest path for reproducible local builds tied to your checkout.

Grounding: [`../../README.md`](../../README.md), [`../../crates/ava-tui/Cargo.toml`](../../crates/ava-tui/Cargo.toml)

## Guided source install from a repo checkout

If you want a more guided source-build path, the repo also ships `install-from-source.sh`.

Examples:

```bash
./install-from-source.sh --cli
./install-from-source.sh --desktop
./install-from-source.sh --all
```

This script is useful for power users who want a one-command source install while still choosing between CLI-only and desktop builds.

Grounding: [`../../install-from-source.sh`](../../install-from-source.sh)

## Install with web support

The `serve` command is feature-gated behind the `web` feature in [`../../crates/ava-tui/Cargo.toml`](../../crates/ava-tui/Cargo.toml).

Install it like this:

```bash
cargo install --path crates/ava-tui --bin ava --features web --force
```

Use `--force` when replacing a previously installed default `ava` binary with the web-enabled build.

## Install AVA Desktop

Desktop is a separate product surface from the CLI.

Use it when you want the Tauri app instead of the terminal-first `ava` binary.

For the dedicated desktop download/build guide, use [How-to: Download AVA Desktop](download-desktop.md).

## Desktop download path

When maintainers publish desktop bundles on a GitHub Release, that release page is the end-user download surface.

Current desktop bundle types produced by the documented Tauri release flow are:

1. Linux: `.deb`, `.AppImage`, `.rpm`
2. macOS: `.dmg`, `.app`
3. Windows: `.msi`, `.exe`

Desktop publishing is still maintained as a manual maintainer flow in this repo, so treat the GitHub Releases page as the source of truth for which desktop bundles are available for a given version.

## Desktop from source

If the release you want does not include a desktop bundle, build it from source:

```bash
./install-from-source.sh --desktop
```

Or use the lower-level Tauri workflow in `src-tauri/`.

See: [Reference: Install and release paths](../reference/install-and-release-paths.md), [Contributing: Releasing](../contributing/releasing.md)

## Windows note

`install.sh` itself is Unix-only and exits on Windows.

On Windows, use one of these paths instead:

1. GitHub Releases for the Windows asset or generated PowerShell installer
2. Source builds from a repo checkout

Grounding: [`../../install.sh`](../../install.sh), [`../../dist-workspace.toml`](../../dist-workspace.toml)

## Prepare the desktop development toolchain

If you want to run the Tauri desktop app from source, install the JavaScript dependencies first:

```bash
pnpm install --reporter=silent
```

The desktop commands live in [`../../package.json`](../../package.json) and the Tauri app lives under `src-tauri/`.

## Verify the install

Run:

```bash
ava --help
ava --version
```

If the command is not found after using `install.sh`, your current shell probably has not reloaded the PATH update yet.

If you built with `--features web`, verify it by actually starting the server:

```bash
ava serve --host 127.0.0.1 --port 8080 --token dev-local-token
```

If you omit `--token`, AVA generates one at startup and prints it. Privileged HTTP routes and `/ws` require that token.

## Next step

Add credentials and run your first prompt:

```bash
ava auth login openrouter
ava
```
