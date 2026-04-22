---
title: "Configuration"
description: "How AVA loads configuration, what the main config sections are, and where precedence applies."
order: 3
updated: "2026-04-21"
---

# Configuration

This page documents AVA's current configuration behavior.

Most users only need `config.yaml`, provider auth, and optional trusted project config. This page is for exact precedence and schema details.

## Configuration Sources And Resolution

This section is exact reference behavior, not a setup guide.

Provider and model selection currently resolves by source in this order:

1. Explicit CLI flags (`--provider`, `--model`)
2. Explicit CLI primary-agent override (`--agent <id>`, resolved from `primary_agents.<id>`)
3. Environment variables (`AVA_PROVIDER`, `AVA_MODEL`)
4. Per-project state (`.ava/state.json`, loaded through `ava_config::ProjectState`)
5. Config default primary agent (`primary_agent` -> `primary_agents.<id>`)
6. Config file fallback (`llm.provider`, `llm.model`)

This resolver short-circuits by source instead of merging missing fields from lower-precedence layers. For example, if `--provider` is set but `--model` is not, AVA does not keep walking the list to fill the model from env vars or config in that resolver path.

## Config Schema (Top-Level)

The main config currently includes:

1. `llm` (provider/model/default generation settings)
2. `editor`
3. `ui`
4. `features`
5. `fallback` (optional)
6. `voice`
7. `claude_code` (optional)
8. `instructions` (extra instruction files)
9. `workspace_roots` (additional roots to index)
10. `primary_agent` (optional default startup primary-agent id)
11. `primary_agents` (optional map of startup primary-agent profiles)
12. `permissions` (`path_rules`)

Example primary-agent section:

```yaml
primary_agent: architect

primary_agents:
  architect:
    provider: openrouter
    model: anthropic/claude-sonnet-4
    prompt: Keep responses architecture-first unless the user asks for direct implementation.
  coder:
    provider: openai
    model: gpt-5.3-codex
```

You can override the active startup profile per run with:

```bash
ava --agent coder
```

## Path Notes

Use `$XDG_CONFIG_HOME/ava` (typically `~/.config/ava`) for user-global config.

Treat `~/.ava` as legacy compatibility input and migrate forward when practical.

## Agent Profile Configuration

Primary agents and subagents use different files:

1. Startup primary-agent profiles: `config.yaml` (`primary_agent` + `primary_agents`)
2. Global delegated subagent profiles: `$XDG_CONFIG_HOME/ava/subagents.toml`
3. Project delegated subagent overrides: `<repo>/.ava/subagents.toml` (trust-gated)

Migration note: delegated-agent config in `agents.toml` is no longer loaded. Rename old files to `subagents.toml`.

For practical setup examples and the subagent TOML structure, use [How-to: Configure primary agents and subagents](../how-to/agents.md).

## Resume And Override Notes

1. `--continue` / `--session` can restore prior session primary-agent metadata (`primaryAgentId` + prompt).
2. Explicit `--agent <id>` still wins over resumed metadata.
3. Explicit `--provider` / `--model` still win over the selected primary-agent provider/model.

## Practical Recommendation

1. Keep user-global config in `$XDG_CONFIG_HOME/ava/config.yaml`.
2. Keep delegated subagent config in `$XDG_CONFIG_HOME/ava/subagents.toml`.
3. Use `<repo>/.ava/subagents.toml` only for trusted project overrides.
4. Keep `~/.ava/*` only as temporary compatibility input during migration.

Treat project-local `.ava/state.json` as session-oriented state rather than a full replacement for user-global config.

## Project-Local State

This is background reference. Most users do not need to interact with this file directly.

Project-local state lives in `.ava/state.json` under the project root.

It stores:

1. `last_provider`
2. `last_model`
3. `recent_models`
4. `plan_model`
5. `code_model`

## Related

1. [Environment variables](environment-variables.md)
2. [Filesystem layout](filesystem-layout.md)
3. [Providers and auth](providers-and-auth.md)
