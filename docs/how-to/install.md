---
title: "How-to: Install AVA"
description: "Build, run, and install AVA with explicit Cargo-first workflows for power users and source-based development."
order: 1
updated: "2026-04-19"
---

# How-to: Install AVA

Use this page when you want explicit Cargo-first workflows: build in place, run from the build output, or install only when you actually want an installed binary.

AVA has two install surfaces:

1. `ava` CLI/TUI for terminal and headless use
2. AVA Desktop for the Tauri app

See also: [Tutorial: First run](../tutorials/first-run.md), [How-to: Download AVA Desktop](download-desktop.md), [How-to: Run AVA locally](run-locally.md), [Reference: Install and release paths](../reference/install-and-release-paths.md)

This page is intentionally written for power users.

If you downloaded the source, the primary workflow is:

1. build with Cargo
2. run the built binary from the build output directory
3. only use `cargo install` if you specifically want an installed binary on your `PATH`

## Quick Path

If you already know what you want, use one of these:

1. Fast binary install on Linux/macOS:

```bash
curl -fsSL https://raw.githubusercontent.com/Artificial-Source/AVA/develop/install.sh | sh
```

2. Manual source build without installing:

```bash
cargo build --release --bin ava
./target/release/ava
```

3. Manual source install:

```bash
cargo install --path crates/ava-tui --bin ava
```

4. Windows or manual binary download:

Open <https://github.com/Artificial-Source/AVA/releases>

## Install AVA CLI

For the CLI, there are four normal paths:

1. Binary install from GitHub Releases using `install.sh`
2. Manual source build using Cargo
3. Manual source install using Cargo
4. Optional convenience wrapper using `install-from-source.sh`

If you downloaded the source and want the normal developer workflow, use `cargo build` first, not `cargo install`.

## For Developers With Their Own Build Workflow

If you already have your own shell functions, build wrappers, or local automation, use these as the primitive commands for this repo.

Build without installing:

```bash
cargo build --release --bin ava
```

Run the built binary:

```bash
./target/release/ava
```

Use a separate build directory:

```bash
CARGO_TARGET_DIR=build cargo build --release --bin ava
./build/release/ava
```

Install the binary only if you explicitly want it on your `PATH`:

```bash
cargo install --path crates/ava-tui --bin ava
```

`install-from-source.sh` is optional convenience only. It is not the canonical build path.

## Prerequisites

For binary install:

1. Linux or macOS
2. `tar`
3. `curl`

For manual source build/install:

1. Rust toolchain with Cargo
2. A checked-out copy of this repository

There is no separate configure step for the CLI.

If you are used to CMake-style projects, Cargo combines the normal build/install behavior into its own commands.

## Fast install from release binaries (Linux/macOS)

Run the repository installer script:

```bash
curl -fsSL https://raw.githubusercontent.com/Artificial-Source/AVA/develop/install.sh | sh
```

This is the best default for most CLI users because it does not require Rust, Node.js, or a repo checkout.

This path downloads a prebuilt binary. It does not compile AVA from source.

What this does:

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
ava --help
```

Grounding: [`../../install.sh`](../../install.sh), [`../../dist-workspace.toml`](../../dist-workspace.toml)

Repo-slug note: release-related links in this checkout are aligned to `Artificial-Source/AVA`.

## Manual binary download from GitHub Releases

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

The release pipeline also generates installer assets. `install.sh` is the Unix entrypoint in this repo; Windows users should use the GitHub Releases page and choose the Windows asset or generated installer for the version they want.

This route is also the fallback for platforms not handled by `install.sh`.

### Asset names

AVA's current CLI release archives follow the target-triple naming used by `cargo-dist`. The installer also accepts the older `ava-...tar.gz` naming from earlier releases.

Examples:

1. macOS Apple Silicon: `ava-tui-aarch64-apple-darwin.tar.xz`
2. macOS Intel: `ava-tui-x86_64-apple-darwin.tar.xz`
3. Linux ARM64: `ava-tui-aarch64-unknown-linux-gnu.tar.xz`
4. Linux x64: `ava-tui-x86_64-unknown-linux-gnu.tar.xz`
5. Windows x64: `ava-tui-x86_64-pc-windows-msvc.zip` or the generated Windows installer on the release page

If you are scanning a release page quickly, these names are the fastest way to map OS and architecture to the right CLI download.

## Build from source without installing

This is the primary path for power users.

Use it when you want to:

1. recompile after changing one file
2. let Cargo rebuild only the minimum necessary work
3. run the new binary immediately
4. avoid installing into `$HOME`, `/usr`, or `/usr/local`

From the repository root:

```bash
cargo build --release --bin ava
```

Default output path:

```text
target/release/ava
```

Run it directly:

```bash
./target/release/ava
```

If you want a dedicated build directory similar to `BUILDDIR`, set `CARGO_TARGET_DIR`:

```bash
CARGO_TARGET_DIR=build cargo build --release --bin ava
./build/release/ava
```

In Cargo terms, `CARGO_TARGET_DIR` is the closest equivalent to a separate build directory in a CMake-style project.

If you are iterating on the code, this is usually the best loop:

```bash
cargo build --release --bin ava
./target/release/ava
```

Cargo already uses dependency tracking and incremental build information, so after a small code change it will typically rebuild only what is necessary.

## Build and install from source

Use this only when you want Cargo to compile the CLI and place an installed `ava` binary on your system path.

In simple terms:

1. There is no separate `configure` step for the CLI
2. `cargo install --path ...` compiles the Rust project from source
3. After compiling, Cargo installs the `ava` binary for you

From the repository root:

```bash
cargo install --path crates/ava-tui --bin ava
```

This is the clearest manual install path when you explicitly want an installed binary instead of running from the build output.

Grounding: [`../../README.md`](../../README.md), [`../../crates/ava-tui/Cargo.toml`](../../crates/ava-tui/Cargo.toml)

## Optional convenience wrapper

The repo also ships `install-from-source.sh`.

This is not a different build system. It is a convenience script for people who want one command instead of typing the manual steps themselves.

If you prefer explicit commands over helper scripts, skip this section and use the Cargo commands above.

Examples:

```bash
./install-from-source.sh --cli
./install-from-source.sh --desktop
./install-from-source.sh --all
```

This script is useful when you want a repo-provided wrapper that handles dependency checks and routes you to CLI-only, desktop-only, or combined source installs.

For most power users, the manual Cargo commands are still the clearer default.

Grounding: [`../../install-from-source.sh`](../../install-from-source.sh)

## Install with web support

The `serve` command is feature-gated behind the `web` feature in [`../../crates/ava-tui/Cargo.toml`](../../crates/ava-tui/Cargo.toml).

Manual source install with web support:

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
2. Manual source build from a repo checkout with Cargo

Grounding: [`../../install.sh`](../../install.sh), [`../../dist-workspace.toml`](../../dist-workspace.toml)

## Desktop development prerequisites

If you want to run the Tauri desktop app from source, install the JavaScript dependencies first:

```bash
pnpm install --reporter=silent
```

The desktop commands live in [`../../package.json`](../../package.json) and the Tauri app lives under `src-tauri/`.

## Verify the CLI

Run:

```bash
ava --help
```

If the command is not found after using `install.sh`, your current shell probably has not reloaded the PATH update yet.

If you used `cargo install`, make sure Cargo's bin directory is on your `PATH`.

If you used `cargo build`, run the binary directly from `target/release/ava` or your chosen `CARGO_TARGET_DIR`.

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
