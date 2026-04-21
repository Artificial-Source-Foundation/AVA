---
title: "Environment Variables"
description: "Environment variables currently consumed by AVA for provider selection, credentials, runtime context, plugin loading, and integration paths."
order: 4
updated: "2026-04-20"
---

# Environment Variables

This page lists env vars that are clearly consumed in current AVA code paths.

It is not a promise that all listed vars are stable public API forever.

## Provider And Model Selection

From `crates/ava-tui/src/config/cli.rs`:

1. `AVA_PROVIDER` - provider fallback when CLI flags are not set
2. `AVA_MODEL` - model fallback when CLI flags are not set

## Provider Credentials

From `crates/ava-config/src/credentials.rs` and `docs/reference/providers-and-auth.md`:

1. `AVA_<PROVIDER>_API_KEY` (provider-specific override, first priority)
2. Standard provider env vars, including:
   - `ANTHROPIC_API_KEY`
   - `OPENAI_API_KEY`
   - `OPENROUTER_API_KEY`
   - `GEMINI_API_KEY`
   - `INCEPTION_API_KEY`
   - `DASHSCOPE_API_KEY`
   - `ZHIPU_API_KEY`
   - `KIMI_API_KEY`
   - `MINIMAX_API_KEY`
   - `OLLAMA_API_KEY`

## Provider Endpoint Override

From `crates/ava-llm/src/providers/mod.rs`:

1. `OLLAMA_BASE_URL` - fallback base URL override for Ollama provider setup

Current Ollama precedence note: provider creation checks saved credential `base_url` first, then `OLLAMA_BASE_URL`, then `http://localhost:11434`.

For an end-to-end setup and runtime verification walkthrough, use [How-to: Use local models with Ollama](../how-to/ollama-local-models.md).

## Credential Encryption / Keychain Flow

From `crates/ava-config/src/keychain.rs`:

1. `AVA_MASTER_PASSWORD` - used before interactive prompt when encrypted credential storage is active

## Workspace / Runtime Integration

From `crates/ava-tools/src/core/path_guard.rs` and `crates/ava-acp/src/session_store.rs`:

1. `AVA_WORKSPACE` - workspace-root override used by path guard
2. `AVA_ACP_SESSION_STORE` - custom ACP session-store path
3. `AVA_PURE` - when set to a truthy value (`1`, `true`, `yes`, `on`), skips all plugin auto-loading at startup, including both `$XDG_CONFIG_HOME/ava/plugins` (legacy `~/.ava/plugins`) and trusted project-local `.ava/plugins`

## Benchmark/Test-Specific Variables

From `crates/ava-tui/src/benchmark_support/workspace.rs`:

1. `AVA_MCP_AUDIT_LOG` - benchmark harness wiring for MCP audit logging

This variable is used by benchmark/test flows to capture MCP interaction logs. It is operational/testing-specific, not part of a documented stable user surface.

## Related

1. [Configuration](configuration.md)
2. [Providers and auth](providers-and-auth.md)
3. [Credential storage](credential-storage.md)
4. [How-to: Use local models with Ollama](../how-to/ollama-local-models.md)
