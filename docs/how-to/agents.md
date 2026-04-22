---
title: "How-to: Configure Primary Agents and Subagents"
description: "Set up startup primary-agent profiles and delegated subagent profiles, including trust and external prompt-file references."
order: 3
updated: "2026-04-21"
---

# How-to: Configure Primary Agents and Subagents

Use this page to configure:

1. **Primary agents** (`config.yaml`) for startup behavior
2. **Subagents** (`subagents.toml`) for delegated specialist runs

## Where these files live

Use `$XDG_CONFIG_HOME/ava` (typically `~/.config/ava`) as the canonical user-global config root.
Keep `~/.ava` only as legacy compatibility input while migrating older setups.

1. Primary-agent profiles: `$XDG_CONFIG_HOME/ava/config.yaml`
2. Global subagent profiles: `$XDG_CONFIG_HOME/ava/subagents.toml`
3. Project subagent overrides: `<repo>/.ava/subagents.toml`

`subagents.toml` is the only delegated-agent config filename AVA loads.

## Migration: `agents.toml` -> `subagents.toml`

If you still have delegated-agent config in either of these files, rename it:

1. `~/.ava/agents.toml` -> `~/.ava/subagents.toml` (or move to `$XDG_CONFIG_HOME/ava/subagents.toml`)
2. `<repo>/.ava/agents.toml` -> `<repo>/.ava/subagents.toml`

AVA does not load `agents.toml` anymore. It emits a warning when it detects legacy files.

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

TUI note:

1. When `primary_agents` are configured, `Tab` / `Shift+Tab` cycle the active startup profile inside the TUI.
2. If no primary-agent profiles are configured, those keys keep their existing Build/Plan mode-cycling behavior.
3. Child subagent transcript views stay read-only and use `Esc` to return to the main conversation.

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

1. `subagents.<id>` is the canonical table form.
2. Subagent entries can override: `description`, `enabled`, `model`, `max_turns`, `prompt`, `prompt_file`, `temperature`, `provider`, `allowed_tools`, `max_budget_usd`.
3. `prompt_file` points to an external text/markdown file. Relative paths resolve from the directory containing that `subagents.toml` file.
4. If `prompt_file` cannot be read, AVA logs a warning and the subagent falls back to its normal prompt resolution path (for example built-in/default prompt behavior, or no custom prompt if none is available).

Example prompt-file usage:

```toml
[subagents.review]
prompt_file = "prompts/review.md"
```

With this project layout:

```text
<repo>/
  .ava/
    subagents.toml
    prompts/
      review.md
```

`prompt_file = "prompts/review.md"` resolves to `<repo>/.ava/prompts/review.md`.

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

1. Project subagent config (`<repo>/.ava/subagents.toml`) is loaded only for trusted projects.
2. For untrusted projects, AVA ignores project-local subagent config and uses built-ins plus global config.
3. Use `--trust` to approve loading project-local `.ava/` config surfaces.

## Related

1. [Reference: Configuration](../reference/configuration.md)
2. [Reference: Commands](../reference/commands.md)
3. [Reference: Filesystem layout](../reference/filesystem-layout.md)
