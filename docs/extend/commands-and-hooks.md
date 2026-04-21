---
title: "Commands And Hooks"
description: "Repo-local slash commands and lifecycle hooks for lightweight workflow automation."
order: 4
updated: "2026-04-08"
---

# Commands And Hooks

Beyond plugins and MCP, AVA supports two lightweight extension surfaces for repo-specific automation: custom slash commands and lifecycle hooks.

## Custom Commands

Custom slash commands are loaded from TOML files.

Load locations:

1. `~/.ava/commands/` - global commands
2. `.ava/commands/` - project-local commands

Project-local commands are only loaded when the project is trusted.

### Schema

```toml
name = "review"
description = "Review code changes for issues"
prompt = """
Review the current git diff.
Focus area: {{focus}}
"""

allowed_tools = ["read", "glob", "grep", "bash"]

[[params]]
name = "focus"
description = "Area to focus on"
required = false
default = "all"
```

Notes:

1. Command names are normalized to lowercase and leading `/` is stripped.
2. Project commands override global commands with the same name.
3. Parameters can be passed as `key=value` pairs or positional arguments.
4. `{{param}}` placeholders are resolved in the prompt before the command is sent to the agent.

## Hooks

Hooks are event-driven TOML configs that fire around agent, tool, and session lifecycles.

Load locations:

1. `~/.ava/hooks/` - global hooks
2. `.ava/hooks/` - project-local hooks

Project-local hooks are only loaded when the project is trusted.

### Schema

```toml
event = "PostToolUse"
description = "Auto-format Rust code after edits"
matcher = "edit|write|multiedit|apply_patch"
path_pattern = "*.rs"
priority = 50
enabled = true

[action]
type = "command"
command = "cargo fmt"
timeout = 10
```

### Hook Action Types

1. `command` - run a shell command via `sh -c`
2. `http` - POST JSON context to an HTTP endpoint
3. `prompt` - reserved LLM-gated action type; currently stubbed and allowed through

### Hook Events

AVA currently exposes 16 hook events:

1. `PreToolUse`
2. `PostToolUse`
3. `PostToolUseFailure`
4. `SessionStart`
5. `SessionEnd`
6. `Stop`
7. `SubagentStart`
8. `SubagentStop`
9. `Notification`
10. `ConfigChange`
11. `PreCompact`
12. `PermissionRequest`
13. `PreModelSwitch`
14. `PostModelSwitch`
15. `BudgetWarning`
16. `UserPromptSubmit`

### Execution Behavior

1. Hooks are sorted by ascending priority.
2. Disabled hooks stay loaded but do not fire.
3. `PreToolUse` hooks can block execution.
4. Hook contexts default to a minimal payload to reduce exfiltration risk.
5. `Prompt` hook actions are currently logged as stubs and do not block.

## TUI Hook Commands

1. `/hooks` or `/hooks list`
2. `/hooks reload`
3. `/hooks dry-run <event> [tool_name]`

## Templates

1. `CustomCommandRegistry::create_templates()` creates `.ava/commands/example.toml`
2. `HookRegistry::create_templates()` creates starter hook files under `.ava/hooks/`
3. `/init` does not currently create commands or hooks templates directly

## Code Paths

1. Custom command schema and precedence: `crates/ava-tui/src/state/custom_commands.rs`
2. Hook schema and loader: `crates/ava-tui/src/hooks/config.rs`
3. Hook event catalog: `crates/ava-tui/src/hooks/events.rs`
4. Hook execution behavior: `crates/ava-tui/src/hooks/runner.rs`
5. Slash-command integration: `crates/ava-tui/src/app/command_support.rs`
