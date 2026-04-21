---
title: "Custom Tools"
description: "Define lightweight TOML-backed tools for project-local or user-local automation."
order: 5
updated: "2026-04-08"
---

# Custom Tools

Custom tools let you add lightweight shell or script-backed tools without building a full plugin.

## Locations

Drop `.toml` files in either location:

1. `~/.ava/tools/` - global tools available in all projects
2. `.ava/tools/` - project-local tools available only in this project

Project tools that collide with an existing tool name are skipped to prevent shadowing built-in, MCP, or previously loaded tools.

Project-local tools are only loaded when the current repo is trusted.

## Basic Format

```toml
name = "tool_name"
description = "What this tool does -- shown to the LLM"

[[params]]
name = "param_name"
type = "string"
required = true
description = "Shown to the LLM"

[execution]
type = "shell"
command = "echo {{param_name}}"
timeout_secs = 30
```

Parameters are substituted with `{{param_name}}` placeholders. Values are shell-escaped to reduce command-injection risk.

## Execution Types

Shell example:

```toml
[execution]
type = "shell"
command = "git log --oneline -{{count}}"
timeout_secs = 10
```

Script example:

```toml
[execution]
type = "script"
interpreter = "python3"
script = """
import sys
print(f"Hello, {{name}}!")
"""
timeout_secs = 15
```

## Templates

`/init` currently creates `.ava/tools/hello.toml` as the starter custom tool.

The broader template helpers live in the tooling code, but `/init` itself only writes the single `hello.toml` example today.

## Implementation

Source: `crates/ava-tools/src/core/custom_tool.rs`
