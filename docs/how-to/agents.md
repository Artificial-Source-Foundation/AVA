---
title: "How-to: Configure Primary Agents and Subagents"
description: "Set up startup primary-agent profiles and delegated subagent profiles, including trust and compatibility behavior."
order: 3
updated: "2026-04-21"
---

# How-to: Configure Primary Agents and Subagents

Use this page to configure:

1. **Primary agents** (`config.yaml`) for startup behavior
2. **Subagents** (`subagents.toml`) for delegated specialist runs

## Where these files live

Canonical user-global config root is `$XDG_CONFIG_HOME/ava` (typically `~/.config/ava`).

Legacy compatibility root `~/.ava` is still read for existing installs and compatibility fallbacks.

1. Primary-agent profiles: `$XDG_CONFIG_HOME/ava/config.yaml`
2. Global subagent profiles: `$XDG_CONFIG_HOME/ava/subagents.toml`
3. Project subagent overrides: `<repo>/.ava/subagents.toml`

Legacy compatibility:

1. `$XDG_CONFIG_HOME/ava/agents.toml` and `<repo>/.ava/agents.toml` are still read as fallback input.
2. If both `subagents.toml` and `agents.toml` exist in the same scope, AVA prefers `subagents.toml`.
3. New writes target `subagents.toml`.

## Configure primary agents (startup profiles)

Add this to `config.yaml`:

```yaml
primary_agent: architect

primary_agents:
  architect:
    provider: openrouter
    model: anthropic/claude-sonnet-4
    prompt: Keep responses architecture-first unless direct implementation is requested.
  coder:
    provider: openai
    model: gpt-5.3-codex
```

Run with an explicit profile:

```bash
ava --agent coder
```

## Configure subagents (delegated specialists)

Create `subagents.toml`:

```toml
[defaults]
enabled = true
model = "anthropic/claude-haiku-4.5"
max_turns = 10

[subagents.review]
enabled = true
description = "Code reviewer"
max_turns = 15

[subagents.explore]
enabled = true
max_turns = 5
```

Notes:

1. `subagents.<id>` is the preferred table form.
2. `agents.<id>` is still accepted for compatibility.
3. Subagent entries can override: `description`, `enabled`, `model`, `max_turns`, `prompt`, `temperature`, `provider`, `allowed_tools`, `max_budget_usd`.

## Built-in/default subagents

AVA currently ships these built-in subagent IDs:

1. `build`
2. `explore`
3. `general`
4. `plan`
5. `review`
6. `scout`
7. `subagent` (default alias when no explicit agent type is supplied)
8. `task`
9. `worker`

Notes:

1. `general` and `worker` are both real shipped built-ins (they are not mutually exclusive placeholders).
2. `subagent` is the canonical default alias when delegation is requested without an explicit type.
3. `task` remains a supported compatibility alias in runtime/tooling paths.

## Using subagents

You do not need a separate command to “enter subagent mode.” The primary agent delegates when needed.

What you will see in practice:

1. A delegated subagent run appears in the conversation with its specialist ID/status.
2. You can open that delegated run to inspect the child session transcript.
3. The parent conversation keeps the delegation link so you can move between parent/child context.
4. If delegation is disabled (`[defaults].enabled = false` or per-subagent `enabled = false`), work stays on the primary agent.

## Resume and override behavior

1. `--continue` / `--session` restore session primary-agent metadata (`primaryAgentId` and prompt) when present.
2. Explicit `--agent <id>` still wins over resumed session metadata.
3. If both `--agent` and `--provider`/`--model` are set, provider/model flags win while the selected primary-agent prompt still applies.

## Trust behavior

1. Project subagent config (`<repo>/.ava/subagents.toml` and legacy `<repo>/.ava/agents.toml`) is loaded only for trusted projects.
2. For untrusted projects, AVA ignores project-local subagent config and uses built-ins plus global config.
3. Use `--trust` to approve loading project-local `.ava/` config surfaces.

## Related

1. [Reference: Configuration](../reference/configuration.md)
2. [Reference: Commands](../reference/commands.md)
3. [Reference: Filesystem layout](../reference/filesystem-layout.md)
