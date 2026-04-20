---
title: "How-to: Install AVA"
description: "Choose the right AVA install path for CLI or desktop use."
order: 1
updated: "2026-04-20"
---

# How-to: Install AVA

Use this page when you want the shortest correct install path.

AVA has two user-facing products:

1. `ava` CLI for terminal use: TUI by default, headless with `--headless`
2. AVA Desktop for the native Tauri app

See also: [Tutorial: First run](../tutorials/first-run.md), [How-to: Download AVA Desktop](download-desktop.md), [Reference: Install and release paths](../reference/install-and-release-paths.md)

## Choose your path

| I want to... | Use this path | Notes |
|---|---|---|
| Install the terminal app quickly on Linux/macOS | One-line `install.sh` installer | No Rust toolchain required |
| Install the terminal app on Windows | GitHub Releases | Download the Windows CLI asset |
| Build the CLI from source | `cargo build --release --bin ava` | Best loop for contributors and power users |
| Install the CLI from source onto your `PATH` | `cargo install --path crates/ava-tui --bin ava` | Installs `ava` globally |
| Use the desktop app | Desktop download/build path | Desktop bundles are not published on every release |
| Use `ava serve` web mode | Web-enabled source build | Requires the `web` feature |

## Fast CLI install on Linux/macOS

```bash
curl -fsSL https://raw.githubusercontent.com/Artificial-Source/AVA/develop/install.sh | sh
```

This is the best default for most CLI users.

Requirements:

1. Linux or macOS
2. `curl`
3. `tar`

If your shell has not picked up the PATH update yet:

```bash
export PATH="$HOME/.ava/bin:$PATH"
ava --help
```

## Windows CLI install

1. Open <https://github.com/Artificial-Source/AVA/releases>
2. Download the Windows CLI asset for the version you want
3. Put the extracted `ava` binary on your `PATH`
4. Verify with `ava --help`

## Build the CLI from source

From the repository root:

```bash
cargo build --release --bin ava
./target/release/ava
```

Use this path when you want a local build without installing anything.

## Install the CLI from source

From the repository root:

```bash
cargo install --path crates/ava-tui --bin ava
```

Use this when you want `ava` available on your `PATH` from a local checkout.

## Install AVA Desktop

Use [How-to: Download AVA Desktop](download-desktop.md).

That page covers:

1. release downloads when available
2. source builds when a release does not include desktop bundles
3. platform notes for the current desktop path

## Install with web support

Web mode is not included in the default CLI build. Build or install with the `web` feature first:

```bash
cargo install --path crates/ava-tui --bin ava --features web --force
ava serve --host 127.0.0.1 --port 8080 --token dev-local-token
```

## What most users should do

1. CLI users: use the fast binary install on Linux/macOS or a GitHub Releases download on Windows
2. Source builders: use `cargo build --release --bin ava`
3. Desktop users: use the dedicated desktop guide

If you want release artifact names, install matrix details, or release-pipeline specifics, use [Reference: Install and release paths](../reference/install-and-release-paths.md).
