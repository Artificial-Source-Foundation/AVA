---
title: "Tutorial: First Run"
description: "Install AVA, add credentials, and launch it for the first time."
order: 1
updated: "2026-04-20"
---

# Tutorial: First Run

This tutorial walks through a real first run.

If you only want the fastest terminal install on Linux/macOS, use:

```bash
curl -fsSL https://raw.githubusercontent.com/Artificial-Source/AVA/develop/install.sh | sh
```

Use the source-install path below when you already have a Rust toolchain and a local checkout.

## Prerequisites

Choose the prerequisites that match your install path:

1. For the release binary path: Linux/macOS plus `curl` and `tar`, or the GitHub Releases download path on Windows
2. For the source install path: a Rust toolchain and a checkout of this repository

## Step 1: Install AVA

Choose one of these paths:

1. Fast CLI binary install on Linux/macOS
2. Windows binary download from <https://github.com/Artificial-Source/AVA/releases>
3. Source install from this checkout using Cargo

### Source install path

From any directory, point Cargo at the local checkout you want to install from:

```bash
CARGO_TARGET_DIR=/path/to/build cargo install --path /path/to/AVA/crates/ava-tui --bin ava
```

Why this command: it installs the `ava` binary from this local checkout onto your `PATH`.

On Windows, use Windows paths instead, for example `C:\build` and `C:\src\AVA\crates\ava-tui`.

## Step 2: Add provider credentials

Log in to a provider (example: OpenRouter):

```bash
ava auth login openrouter
```

You can use other supported provider IDs listed in [Reference: Providers and auth](../reference/providers-and-auth.md).

## Step 3: Start AVA

Pick one run mode:

```bash
ava
```

or start a headless run with an initial goal:

```bash
ava "summarize this repository" --headless
```

If you installed AVA with the default command above, use the TUI or headless mode first.

Web mode needs a build that includes the `web` feature:

```bash
CARGO_TARGET_DIR=/path/to/build cargo install --path /path/to/AVA/crates/ava-tui --bin ava --features web --force
ava serve --host 127.0.0.1 --port 8080 --token dev-local-token
```

If you omit `--token`, AVA generates one at startup. Browser WebSocket and control-plane clients need that token.

## What to verify

1. AVA starts without auth errors.
2. Your first prompt returns a model response.
3. A real one-turn run succeeds for the provider you intend to use, for example `ava --provider openrouter --model anthropic/claude-sonnet-4 --headless --max-turns 1 "Reply with OK"`.
4. If you enabled web mode, `ava serve` starts an HTTP server instead of returning a feature-gate error.

## Next step

Continue to [Tutorial: Your First Workflow](your-first-workflow.md).
