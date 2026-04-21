---
title: "Credential Storage"
description: "Where AVA stores credentials and the recommended security posture for provider auth."
order: 4
updated: "2026-04-18"
---

# Credential Storage

AVA does not hardcode provider API keys in source code.

## Where AVA stores credentials

AVA supports multiple credential paths:

1. Environment variables such as `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`, and provider-specific `AVA_<PROVIDER>_API_KEY`
2. User-local credential storage under `$XDG_CONFIG_HOME/ava/credentials.json` (legacy `~/.ava/credentials.json` compatibility)
3. Optional OS keychain / encrypted storage paths in `ava-config`

The common default file-backed path is user-local, not repo-local.

## Security guidance

Preferred order:

1. OS keychain or encrypted local storage
2. Environment variables for ephemeral sessions
3. Plaintext local file storage only when needed

If you manually keep secrets in the file-backed credential store (`$XDG_CONFIG_HOME/ava/credentials.json` or legacy `~/.ava/credentials.json`), treat it as sensitive local state.

## Comparison

| Project | Primary storage | Encrypted by default | Env-var support | Notes |
|---|---|---:|---:|---|
| AVA | `$XDG_CONFIG_HOME/ava/credentials.json` (legacy `~/.ava/credentials.json`) plus keychain/encrypted support | Partial / supported | Yes | User-local file store exists, but AVA also has stronger secure-storage paths available |
| OpenCode | user-local plaintext JSON | No | Limited | Stores auth in a plaintext local JSON file with restrictive permissions |
| PI | `~/.pi/agent/auth.json` plaintext JSON | No | Yes | Plaintext local JSON with file locking and env-var fallback |

## Source paths

Relevant AVA code:

1. `crates/ava-config/src/credentials.rs`
2. `crates/ava-config/src/keychain.rs`
3. `src-tauri/src/commands/config_commands.rs`

Reference-code snapshots used for comparison:

1. `docs/reference-code/opencode/packages/opencode/src/auth/index.ts`
2. `docs/reference-code/pi-mono/packages/coding-agent/src/core/auth-storage.ts`

Those comparisons are based on reference snapshots in this repository as of `2026-04-18`; upstream external projects may have changed since those snapshots were taken.

## Related

1. [Providers and auth](providers-and-auth.md)
2. [Environment variables](environment-variables.md)
3. [Filesystem layout](filesystem-layout.md)
