<!-- Last verified: 2026-04-02 -->

# AVA Extensions Guide

AVA supports several extension layers. They solve different problems and can be combined:

1. **MCP servers** for external tool ecosystems
2. **Custom tools** for lightweight AVA-native tools
3. **Custom slash commands** for reusable prompt workflows
4. **Skills and instructions** for behavioral guidance
5. **Power plugins** for deep runtime hooks and auth/tool/chat integration

If you only need one more tool quickly, start with **custom tools** or **MCP**. If you need to shape how AVA behaves on a project, start with **skills/instructions**. If you need to intercept runtime events or auth flows, use **power plugins**.

## At A Glance

| Extension type | Best for | Scope | Trust required for project-local files? |
|------|---------|-------|-------------------------------------------|
| MCP server | Reusing an external tool server | global or project | yes |
| Custom tool | Adding one focused shell/script tool | global or project | yes |
| Custom slash command | Reusable workflow prompts like `/review` | global or project | yes |
| Skill / instruction | Teaching AVA how to behave | global or project | yes |
| Power plugin | Hooking auth, tool execution, prompt flow, events | global or project | yes |

Global files under `~/.ava/` always load. Project-local files under `.ava/` load only when the workspace is trusted. Use `ava --trust` to approve a project.

## 1. MCP Servers

MCP is the most practical path when you want AVA to talk to an existing external tool server.

Create `mcp.json` in either location:

- `~/.ava/mcp.json`
- `.ava/mcp.json`

Project-local config overrides global config by server name.

### Example

```json
{
  "servers": [
    {
      "name": "filesystem",
      "transport": {
        "type": "stdio",
        "command": "npx",
        "args": ["-y", "@modelcontextprotocol/server-filesystem", "/tmp"]
      }
    },
    {
      "name": "remote-tools",
      "transport": {
        "type": "http",
        "url": "http://localhost:8080"
      }
    }
  ]
}
```

### Supported transports

- `stdio`
- `http`

### Built-in management

AVA includes slash-command management for MCP:

- `/mcp`
- `/mcp list`
- `/mcp reload`
- `/mcp enable <name>`
- `/mcp disable <name>`

### Notes

- `enabled` defaults to `true`
- project-local `.ava/mcp.json` is trust-gated
- MCP is usually the best choice when the tool ecosystem already exists elsewhere

Implementation:

- `crates/ava-mcp/src/config.rs`
- `crates/ava-mcp/src/client.rs`
- `crates/ava-mcp/src/transport.rs`

## 2. Custom Tools

Custom tools are AVA-native tools defined in TOML. They are the fastest way to add one focused capability without writing a full plugin.

Drop `.toml` files in:

- `~/.ava/tools/`
- `.ava/tools/`

Project tools do not shadow built-in or already-loaded tools with the same name.

### Example

```toml
name = "git_stats"
description = "Show git repository statistics"

[execution]
type = "shell"
command = "git log --oneline -10 && git status --short"
timeout_secs = 10
```

### Parameterized example

```toml
name = "file_count"
description = "Count files matching a pattern"

[[params]]
name = "pattern"
type = "string"
required = true
description = "Glob pattern to match"

[execution]
type = "shell"
command = "find . -name '{{pattern}}' -type f | wc -l"
timeout_secs = 10
```

### Execution modes

- `shell` via `sh -c`
- `script` with an explicit interpreter

Parameter placeholders use `{{param_name}}`. Values are shell-escaped before substitution.

### Bootstrap

Run `/init` in the TUI to generate starter files in `.ava/tools/`.

Implementation:

- `crates/ava-tools/src/core/custom_tool.rs`

## 3. Custom Slash Commands

Custom slash commands let you define reusable prompt workflows like `/review`, `/security`, or `/release-notes` without adding a new tool.

Drop `.toml` files in:

- `~/.ava/commands/`
- `.ava/commands/`

Project commands override global commands with the same name.

### Example

```toml
name = "review"
description = "Review code changes for issues"
prompt = """
Review the current git diff for:
- security vulnerabilities
- performance issues
- missing error handling

Focus area: {{focus}}
"""

[[params]]
name = "focus"
description = "Area to focus on"
required = false
default = "all"

# Optional: restrict what the agent can use
# allowed_tools = ["read", "glob", "grep", "bash"]
```

### Argument style

Commands support:

- named args: `/review focus=security`
- positional args: `/review security`
- quoted strings: `/review focus="auth and session state"`

### What they do

AVA resolves the command template into a prompt, then routes that into the normal agent flow.

Implementation:

- `crates/ava-tui/src/state/custom_commands.rs`

## 4. Skills And Instructions

Skills and instruction files are the main way to shape AVA behavior for a project without writing code.

AVA loads instruction content from several sources and injects it into the agent prompt.

### Main sources

Global:

- `~/.ava/AGENTS.md`
- global skills under the supported skill directories

Project-local:

- ancestor `AGENTS.md` files down to the repo boundary
- project `AGENTS.md`
- `.cursorrules`
- `.github/copilot-instructions.md`
- `.ava/AGENTS.md`
- `.ava/rules/*.md`
- extra `instructions:` paths/globs from config
- skills under supported skill directories

### Supported skill directories

- `.claude/skills`
- `.agents/skills`
- `.ava/skills`

Supported skill layout:

- `<dir>/SKILL.md`
- `<dir>/<skill-name>/SKILL.md`

### Notes

- global skills always load
- project-local skills are trust-gated
- project-local instruction files are trust-gated
- AVA deduplicates files by canonical path
- project-local skill loading includes symlink-boundary checks

Implementation:

- `crates/ava-agent/src/instructions.rs`

## 5. Power Plugins

Power plugins are AVA’s deepest extension layer. They are standalone processes that speak JSON-RPC 2.0 over stdio and subscribe to runtime hooks.

Install plugins in:

- `~/.ava/plugins/<name>/`
- `.ava/plugins/<name>/`

Manage them with:

- `ava plugin list`
- `ava plugin add <path-or-package>`
- `ava plugin remove <name>`
- `ava plugin info <name>`
- `ava plugin init <name> --lang <typescript|python|shell>`

### SDKs

This repo includes:

- TypeScript SDK: `plugins/sdk/`
- Python SDK: `plugins/sdk-python/`

### Example plugin manifest

```toml
[plugin]
name = "my-plugin"
version = "0.1.0"
description = "My AVA plugin"

[runtime]
command = "node"
args = ["index.js"]

[hooks]
subscribe = ["tool.before", "session.start"]
```

### Hook surface

The shipped hook system includes support for:

- `auth`
- `auth.methods`
- `auth.authorize`
- `auth.refresh`
- `request.headers`
- `tool.before`
- `tool.after`
- `tool.definition`
- `chat.params`
- `permission.ask`
- `chat.system`
- `agent.before`
- `agent.after`
- `session.start`
- `session.end`
- `config`
- `event`
- `shell.env`
- `chat.messages.transform`
- `session.compacting`
- `chat.message`
- `text.complete`
- `command.execute.before`

That means plugins can:

- provide or refresh auth credentials
- inject headers into provider calls
- block or rewrite tool calls
- modify tool definitions before the LLM sees them
- influence chat params or system prompt injection
- programmatically allow or deny permission requests
- mutate outgoing message history
- inject shell environment variables
- react to session and agent lifecycle events
- block slash commands before they run

### Current examples in this repo

- `plugins/examples/hello-plugin`
- `plugins/examples/hello-python`
- `plugins/examples/request-logger`
- `plugins/examples/tool-timer`
- `plugins/examples/env-guard`
- `plugins/examples/copilot-auth`
- `plugins/examples/code-stats`

### Practical read

Power plugins are real and capable, but the ecosystem is still early. The platform is stronger than the current public catalog.

Implementation:

- `crates/ava-plugin/src/manager.rs`
- `crates/ava-plugin/src/hooks.rs`
- `crates/ava-plugin/src/discovery.rs`
- `crates/ava-tui/src/plugin_commands.rs`

## Which Extension Path Should I Use?

Use:

- **MCP** when an external tool server already exists
- **Custom tools** when you want one lightweight shell/script tool
- **Custom slash commands** when you want reusable agent workflows
- **Skills/instructions** when you want to shape AVA behavior and policy
- **Power plugins** when you need deep runtime hooks or auth/chat/tool integration

In practice, most teams should start with this order:

1. skills/instructions
2. custom tools
3. MCP
4. custom slash commands
5. power plugins

## Trust Model

Project-local extension files are intentionally gated behind trust because they can affect tool loading, instructions, prompts, and external processes.

Common trust-gated project-local locations:

- `.ava/mcp.json`
- `.ava/tools/`
- `.ava/commands/`
- `.ava/plugins/`
- `.ava/skills/`
- `.ava/rules/`
- project `AGENTS.md` and related instruction files

Approve a project with:

```bash
ava --trust
```

## Related Docs

- [install.md](install.md)
- [hq/README.md](hq/README.md)
- [releasing.md](releasing.md)
- [../AGENTS.md](../AGENTS.md)
- [../CLAUDE.md](../CLAUDE.md)
