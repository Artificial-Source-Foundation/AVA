---
title: "How-to: Run AVA Locally"
description: "Run AVA in TUI, headless, desktop, or web mode from a local checkout."
order: 3
updated: "2026-04-20"
---

# How-to: Run AVA Locally

Use this page when AVA is installed and configured and you want the right local run command.

See also: [How-to: Install AVA](install.md), [How-to: Configure AVA](configure.md), [How-to: Run AVA in CI/headless automation](ci-headless-automation.md)

## Choose Your Run Mode

| I want to... | Command | Notes |
|---|---|---|
| Start the normal interactive terminal UI | `ava` | Default CLI experience |
| Start with an initial goal | `ava "summarize this repository"` | Starts the TUI with an initial task |
| Run non-interactively | `ava "summarize this repository" --headless` | Best for scripts and quick one-shot runs |
| Run from a source checkout without installing | `cargo run --bin ava --` | Or use the `just` helpers below |
| Run the desktop app from source | `pnpm tauri dev` | Requires the frontend/Tauri toolchain |
| Run web mode | `ava serve ...` | Requires a CLI build with the `web` feature |

## Run the TUI

```bash
ava
```

This is the default local experience.

## Run with an initial goal

```bash
ava "summarize this repository"
```

This starts AVA with an initial goal. For an explicit headless run, use:

```bash
ava "summarize this repository" --headless
```

For more flags, use [Reference: Commands](../reference/commands.md).

## Run from source without installing

Use the repo `Justfile` helpers if you want shorter local commands:

```bash
just run
just headless "summarize this repository"
```

Without `just`, use the equivalent Cargo commands:

```bash
cargo run --bin ava --
cargo run --bin ava -- "summarize this repository" --headless
```

## Run the desktop app in development

Use this when you want the native Tauri desktop shell from a local checkout.

```bash
pnpm tauri dev
```

This is the normal local desktop development path.

## Run the web server

If AVA was built with `--features web`, run:

```bash
ava serve --host 127.0.0.1 --port 8080 --token dev-local-token
```

Notes:

1. If you omit `--token`, AVA generates one at startup; the raw value is shown only on the live terminal and redacted from normal logs.
2. Sensitive HTTP session/control routes require `Authorization: Bearer <token>` (or `x-ava-token`).
3. Browser WebSocket clients use `ws://127.0.0.1:8080/ws?token=<token>` (`access_token` also works as a query alias).
4. If you run the Vite frontend against `ava serve`, set `VITE_AVA_SERVER_TOKEN=<token>` or open the frontend once with `?ava_token=<token>` so the browser can authenticate privileged routes.
5. Default browser-origin policy is loopback-only. `--insecure-open-cors` is an explicit opt-in override if you intentionally need non-loopback browser origins during development.

For the full command surface, use [Reference: Commands](../reference/commands.md) and [Reference: Web API surface](../reference/web-api.md).
