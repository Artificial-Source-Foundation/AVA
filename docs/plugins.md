<!-- Last verified: 2026-03-26 -->

# AVA Plugins and Extensions

AVA supports two primary plugin mechanisms for adding tools at runtime: **TOML Custom Tools** and **MCP Servers**. Both work in the CLI/TUI and desktop app.

## TOML Custom Tools

Drop `.toml` files in either location:

- `~/.ava/tools/` -- global tools (available in all projects)
- `.ava/tools/` -- project-local tools (available only in this project)

Project tools that collide with an existing tool name (built-in, MCP, or previously loaded) are skipped to prevent shadowing.

### Format

```toml
name = "tool_name"
description = "What this tool does -- shown to the LLM"

[[params]]
name = "param_name"
type = "string"          # optional, defaults to "string"
required = true
description = "Shown to the LLM"

[execution]
type = "shell"           # or "script"
command = "echo {{param_name}}"
timeout_secs = 30        # optional, defaults to 30
```

Parameters are substituted into the command via `{{param_name}}` placeholders. All values are shell-escaped (single-quoted with internal quotes escaped) to prevent command injection.

### Execution Types

**Shell** -- runs via `sh -c`:

```toml
[execution]
type = "shell"
command = "git log --oneline -{{count}}"
timeout_secs = 10
```

**Script** -- runs via a specified interpreter:

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

### Complete Examples

**Git statistics tool:**

```toml
name = "git_stats"
description = "Show git repository statistics"

[execution]
type = "shell"
command = "echo '=== Commits ===' && git log --oneline -10 && echo '\n=== Status ===' && git status --short"
timeout_secs = 10
```

**File counter with parameter:**

```toml
name = "file_count"
description = "Count files matching a pattern"

[[params]]
name = "pattern"
type = "string"
required = true
description = "Glob pattern to match (e.g. '*.rs')"

[execution]
type = "shell"
command = "find . -name '{{pattern}}' -type f | wc -l"
timeout_secs = 10
```

**Deployment tool:**

```toml
name = "deploy"
description = "Deploy to a target environment"

[[params]]
name = "env"
type = "string"
required = true
description = "Target environment (staging, production)"

[[params]]
name = "branch"
type = "string"
required = false
description = "Branch to deploy (defaults to current)"

[execution]
type = "shell"
command = "scripts/deploy.sh --env={{env}} --branch={{branch}}"
timeout_secs = 300
```

### Generating Templates

Run `/init` in the TUI or use the bootstrap command to create example tool files in `.ava/tools/`. Three templates are created: `hello.toml`, `git-stats.toml`, `file-count.toml`.

### Implementation

Source: `crates/ava-tools/src/core/custom_tool.rs`

---

## MCP Servers

AVA connects to [Model Context Protocol](https://modelcontextprotocol.io/) servers, which expose tools that the agent can call like built-in tools.

### Configuration

Create `mcp.json` in either location:

- `~/.ava/mcp.json` -- global servers (all projects)
- `.ava/mcp.json` -- project-local servers

Project configs override global configs by server name.

**Note**: Project-local `.ava/mcp.json` requires workspace trust. Run `ava --trust` to approve a project.

### Format

```json
{
  "servers": [
    {
      "name": "server-name",
      "transport": { ... },
      "enabled": true
    }
  ]
}
```

The `enabled` field defaults to `true` and can be set to `false` to temporarily disable a server without removing its config.

### Transport Types

**stdio** -- spawns a subprocess and communicates via stdin/stdout:

```json
{
  "name": "filesystem",
  "transport": {
    "type": "stdio",
    "command": "npx",
    "args": ["-y", "@modelcontextprotocol/server-filesystem", "/home/user/projects"],
    "env": {
      "NODE_ENV": "production"
    }
  }
}
```

**http** -- connects to a running HTTP server:

```json
{
  "name": "remote-tools",
  "transport": {
    "type": "http",
    "url": "http://localhost:8080"
  }
}
```

### Complete Example

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
      "name": "github",
      "transport": {
        "type": "stdio",
        "command": "npx",
        "args": ["-y", "@modelcontextprotocol/server-github"],
        "env": {
          "GITHUB_TOKEN": "ghp_..."
        }
      }
    },
    {
      "name": "postgres",
      "transport": {
        "type": "stdio",
        "command": "npx",
        "args": ["-y", "@modelcontextprotocol/server-postgres", "postgresql://localhost/mydb"]
      }
    }
  ]
}
```

### Managing MCP Servers

In the TUI, use the `/mcp` slash command:

| Command | Action |
|---------|--------|
| `/mcp` or `/mcp list` | List configured servers and their status |
| `/mcp reload` | Reload configs and reconnect |
| `/mcp enable <name>` | Enable a server |
| `/mcp disable <name>` | Disable a server |

### Implementation

Source: `crates/ava-mcp/src/config.rs` (config loading), `crates/ava-mcp/src/client.rs` (client), `crates/ava-mcp/src/transport.rs` (transports)

---

## Project Instructions

AVA auto-discovers instruction files and injects them into the agent's system prompt. These are not "plugins" per se but are the primary way to customize agent behavior per project.

### Discovery Order

1. `~/.ava/AGENTS.md` -- global instructions
2. Ancestor walk: `AGENTS.md` and `CLAUDE.md` from outermost ancestor down to `.git` boundary
3. Project root: `AGENTS.md`, `CLAUDE.md`, `.cursorrules`, `.github/copilot-instructions.md`
4. `.ava/AGENTS.md` -- project-local override
5. `.ava/rules/*.md` -- modular rules (alphabetical)
6. `instructions:` paths/globs in `config.yaml`
7. Skill files from `.claude/skills/`, `.agents/skills/`, `.ava/skills/`

Files are plain markdown. Each is prefixed with `# From: <filepath>` in the prompt. Duplicate paths are deduplicated.

### Implementation

Source: `crates/ava-agent/src/instructions.rs`
