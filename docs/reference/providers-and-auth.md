---
title: "Providers And Auth"
description: "Canonical provider IDs, aliases, base URLs, and authentication behavior in AVA."
order: 2
updated: "2026-04-21"
---

# Providers And Auth

This page is the retrieval-first reference for AVA provider IDs, aliases, credential lookup, and authentication surfaces.

## Canonical Providers

AVA recognizes these provider IDs at runtime:

1. `anthropic`
2. `openai`
3. `openrouter`
4. `copilot`
5. `gemini`
6. `inception`
7. `alibaba`
8. `zai`
9. `kimi`
10. `minimax`
11. `ollama`

## Provider Aliases

These aliases are normalized to the canonical IDs above:

1. `chatgpt` -> `openai`
2. `google` -> `gemini`
3. `alibaba-cn` -> `alibaba`
4. `zhipuai-coding-plan` and `zai-coding-plan` -> `zai`
5. `kimi-for-coding` -> `kimi`
6. `minimax-coding-plan` and `minimax-cn-coding-plan` -> `minimax`

## Provider Routing Notes

1. `anthropic`, `openai`, `openrouter`, `copilot`, `gemini`, `inception`, and `ollama` use native provider modules.
2. `alibaba`, `kimi`, and `minimax` use the Anthropic-compatible adapter with provider-specific base URLs.
3. `zai` uses the OpenAI-compatible adapter with provider-specific thinking-format handling.

## Default Base URLs

1. `anthropic` -> `https://api.anthropic.com`
2. `openai` -> `https://api.openai.com`
3. `openrouter` -> `https://openrouter.ai/api`
4. `gemini` -> `https://generativelanguage.googleapis.com`
5. `copilot` -> `https://api.individual.githubcopilot.com`
6. `inception` -> `https://api.inceptionlabs.ai`
7. `ollama` -> `http://localhost:11434`
8. `alibaba` -> `https://coding-intl.dashscope.aliyuncs.com/apps/anthropic/v1`
9. `alibaba-cn` -> `https://coding.dashscope.aliyuncs.com/apps/anthropic/v1`
10. `zai` -> `https://api.z.ai/api/coding/paas/v4`
11. `kimi` -> `https://api.kimi.com/coding/v1`
12. `minimax` -> `https://api.minimax.io/anthropic/v1`

## Credential Lookup Order

AVA resolves credentials in this order:

1. `AVA_<PROVIDER>_API_KEY`
2. Standard provider env var such as `OPENAI_API_KEY`
3. `$XDG_CONFIG_HOME/ava/credentials.json` (legacy `~/.ava/credentials.json` compatibility)

Examples:

1. `AVA_OPENAI_API_KEY`
2. `OPENAI_API_KEY`
3. `ANTHROPIC_API_KEY`
4. `OPENROUTER_API_KEY`
5. `GEMINI_API_KEY`
6. `INCEPTION_API_KEY`
7. `DASHSCOPE_API_KEY`
8. `ZHIPU_API_KEY`
9. `KIMI_API_KEY`
10. `MINIMAX_API_KEY`
11. `OLLAMA_API_KEY`

## OAuth And API Keys

Credential entries can store both API-key and OAuth fields.

1. If a valid OAuth token exists, it is preferred over `api_key`.
2. OAuth tokens are treated as expired 30 seconds before their actual expiry timestamp.
3. If an OAuth token is expired and a refresh token is present, AVA can attempt refresh flows before falling back.
4. OpenAI exposes three user-facing auth choices in the TUI: ChatGPT browser login, ChatGPT headless login, and manual API key entry.
5. `ollama` is treated as a local endpoint and can be considered configured with only a base URL.

For an Ollama-only, task-oriented setup path, use [How-to: Use local models with Ollama](../how-to/ollama-local-models.md).

## User-Facing Auth Surfaces

In chat or TUI slash commands:

1. `/connect [provider]`
2. `/disconnect <provider>`
3. `/providers`

On the CLI:

1. `ava auth login <provider>`
2. `ava auth logout <provider>`
3. `ava auth list`
4. `ava auth test <provider>`

## Files And Code Paths

1. Provider normalization and routing: `crates/ava-llm/src/providers/mod.rs`
2. Credential lookup and env overrides: `crates/ava-config/src/credentials.rs`
3. CLI credential commands: `crates/ava-config/src/credential_commands.rs`
4. CLI auth subcommands: `crates/ava-tui/src/config/cli.rs`

## Related

1. [Environment variables](environment-variables.md)
2. [Configuration](configuration.md)
3. [Credential storage](credential-storage.md)
4. [How-to: Use local models with Ollama](../how-to/ollama-local-models.md)
5. [Troubleshooting: Ollama local models](../troubleshooting/ollama-local-models.md)
