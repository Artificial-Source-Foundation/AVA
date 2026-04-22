---
title: "Common Errors"
description: "Fast fixes for common AVA auth, provider, and CLI startup errors."
order: 2
updated: "2026-04-20"
---

# Common Errors

This page covers frequent AVA errors that are not Linux desktop-rendering specific.

Use this page for quick fixes. If you want the normal setup flow first, start with [How-to: Install AVA](../how-to/install.md), [How-to: Configure AVA](../how-to/configure.md), or [How-to: Run AVA Locally](../how-to/run-locally.md).

For desktop/Linux-specific setup and rendering issues, use:

1. [Tauri Linux toolchain checklist](tauri-toolchain-checklist.md)
2. [WebKitGTK rendering issues](webkitgtk-rendering.md)

## Choose by symptom

| Symptom | Jump to |
|---|---|
| No provider configured | [1) `No provider configured`](#1-no-provider-configured) |
| Unknown provider during login or startup | [2) `Unknown provider: <id>` during `ava auth login`](#2-unknown-provider-id-during-ava-auth-login) or [5) `unknown provider` while creating a provider](#5-unknown-provider-while-creating-a-provider) |
| Credentials missing or empty | [3) `<Provider> credentials are not configured` (or API key empty)](#3-provider-credentials-are-not-configured-or-api-key-empty) |
| OpenAI OAuth expired | [4) OpenAI OAuth expired](#4-openai-oauth-expired) |
| `ava serve` fails (advanced web mode) | [6) `ava serve` fails because web feature is missing](#6-ava-serve-fails-because-web-feature-is-missing) |
| Ollama run fails | [7) Ollama local-model run fails (endpoint/model mismatch)](#7-ollama-local-model-run-fails-endpointmodel-mismatch) |

## 1) `No provider configured`

**Symptom**

```text
No provider configured. Set defaults in $XDG_CONFIG_HOME/ava/config.yaml (legacy ~/.ava/config.yaml still works) or use --provider/--model flags.
```

**Fix**

1. Set a default in `$XDG_CONFIG_HOME/ava/config.yaml` for fresh installs, or `~/.ava/config.yaml` if you are still on the legacy path, or
2. Pass flags explicitly:

```bash
ava --provider openrouter --model anthropic/claude-sonnet-4 "your goal"
```

See: [How-to: Configure AVA](../how-to/configure.md), [Configuration](../reference/configuration.md)

## 2) `Unknown provider: <id>` during `ava auth login`

**Symptom**

```text
Unknown provider: <id>
Available: ...
```

**Fix**

Use one of the canonical provider IDs listed in:

- [Providers and auth](../reference/providers-and-auth.md)

If you used an alias, verify it is one of the currently-supported aliases on that page.

## 3) `<Provider> credentials are not configured` (or API key empty)

**Symptom**

```text
Anthropic credentials are not configured
```

or

```text
<Provider> API key is empty
```

**Fix**

1. Log in again (`ava auth login <provider>`)
2. Use `ava auth list` to inspect the resolved provider status view
3. Treat `ava auth test <provider>` as a local config check only
4. Use a real runtime invocation as the authoritative verification path

See: [How-to: Configure AVA](../how-to/configure.md), [Credential storage](../reference/credential-storage.md)

## 4) OpenAI OAuth expired

**Symptom**

```text
OpenAI OAuth token has expired. Reconnect with /connect openai or set an API key in $XDG_CONFIG_HOME/ava/credentials.json (legacy ~/.ava/credentials.json still works)
```

**Fix**

1. Reconnect OpenAI (`ava auth login openai` or `/connect openai`), or
2. Set a valid OpenAI API key

See: [Providers and auth](../reference/providers-and-auth.md)

## 5) `unknown provider` while creating a provider

**Symptom**

```text
unknown provider. Available core providers: ...
```

**Fix**

Use a supported provider ID and rerun. If you are using a custom alias, map it to a canonical provider supported by AVA.

See: [Providers and auth](../reference/providers-and-auth.md)

## 6) `ava serve` fails because web feature is missing

**Symptom**

```text
Web server requires the 'web' feature. Rebuild with:
  cargo build -p ava-tui --features web
```

**Fix**

If you are using a release-installed `ava` binary, treat `ava serve` as unavailable in that install unless you switch to a source-built CLI with the `web` feature enabled. Most users can ignore this entirely unless they intentionally need the advanced web mode.

For a source checkout, reinstall or run it with the `web` feature enabled:

```bash
CARGO_TARGET_DIR=/path/to/build cargo install --path /path/to/AVA/crates/ava-tui --bin ava --features web --force
```

Or run directly from source with a web-enabled cargo path:

```bash
cargo run --manifest-path /path/to/AVA/Cargo.toml -p ava-tui --features web -- serve --host 127.0.0.1 --port 8080
```

See: [How-to: Install AVA](../how-to/install.md), [How-to: Run AVA locally](../how-to/run-locally.md)

## 7) Ollama local-model run fails (endpoint/model mismatch)

**Symptom**

`--provider ollama` runs fail even though Ollama appears configured.

**Fix**

Use the focused Ollama troubleshooting flow:

- [Troubleshooting: Ollama local models](ollama-local-models.md)

That page covers endpoint reachability, model-name mismatches, and why `ava auth test ollama` can pass while runtime generation still fails.
