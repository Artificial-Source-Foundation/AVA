# OpenCode Agents

> Agents are AI assistants with specific roles, models, and tool access.

---

## Agent Types

| Type | Description | Access |
|------|-------------|--------|
| **Primary** | Main assistants accessed directly | Tab key or keybinds |
| **Subagent** | Specialists invoked by primary agents or `@` mention | Called via Task tool |

### Built-in Agents

| Agent | Type | Description |
|-------|------|-------------|
| `build` | Primary | Full tool access, main executor |
| `plan` | Primary | Restricted tools (no file edits, bash asks) |
| `general` | Subagent | Full capabilities for multi-step work |
| `explore` | Subagent | Read-only for fast codebase exploration |

---

## Agent Definition Methods

### Method 1: JSON in opencode.json

```json
{
  "agent": {
    "review": {
      "description": "Reviews code for quality",
      "mode": "subagent",
      "model": "anthropic/claude-sonnet-4-20250514",
      "prompt": "You are a code reviewer...",
      "temperature": 0.1,
      "tools": {
        "write": false,
        "bash": false
      }
    }
  }
}
```

### Method 2: Markdown Files

Create files in:
- `.opencode/agents/` (project)
- `~/.config/opencode/agents/` (global)

Filename becomes agent ID (e.g., `review.md` → `review` agent).

```markdown
---
description: Reviews code for quality
mode: subagent
model: anthropic/claude-sonnet-4-20250514
temperature: 0.1
tools:
  write: false
  bash: false
---

You are a code reviewer. Analyze code for:
- Security vulnerabilities
- Performance issues
- Best practices violations

Provide actionable feedback with specific line references.
```

---

## Configuration Options

| Option | Type | Description |
|--------|------|-------------|
| `description` | string | Brief explanation (required) |
| `mode` | `primary` \| `subagent` \| `all` | Agent type |
| `model` | string | Model identifier (e.g., `anthropic/claude-sonnet-4`) |
| `temperature` | number | 0.0-1.0, lower = more deterministic |
| `max_steps` | number | Limit agentic iterations |
| `prompt` | string | System prompt (or `{file:./path}`) |
| `tools` | object | Enable/disable tools |
| `permission` | object | Tool permission overrides |
| `task` | object | Subagent invocation permissions |
| `hidden` | boolean | Hide from `@` autocomplete |

---

## Tool Access Control

```json
{
  "agent": {
    "my-agent": {
      "tools": {
        "write": true,
        "bash": false,
        "mymcp_*": true
      }
    }
  }
}
```

Wildcards supported: `*` matches any characters.

---

## Permission Levels

```json
{
  "agent": {
    "my-agent": {
      "permission": {
        "bash": "ask",
        "write": "allow",
        "read": "allow"
      }
    }
  }
}
```

| Level | Behavior |
|-------|----------|
| `allow` | Immediate access |
| `ask` | Prompts user for approval |
| `deny` | Tool unavailable |

### Bash Command Patterns

```json
{
  "permission": {
    "bash(npm test*)": "allow",
    "bash(rm -rf*)": "deny"
  }
}
```

---

## Task Permissions (Subagent Control)

Control which subagents a primary agent can invoke:

```json
{
  "agent": {
    "build": {
      "task": {
        "*": "allow",
        "dangerous-agent": "deny"
      }
    }
  }
}
```

Rules evaluated in order; last match wins.

---

## File Reference in Prompts

Load prompt from external file:

```json
{
  "agent": {
    "my-agent": {
      "prompt": "{file:./prompts/my-agent.txt}"
    }
  }
}
```

Or in markdown frontmatter:
```yaml
---
prompt: "{file:./prompts/my-agent.txt}"
---
```

---

## Example: Delta9 Commander Agent

```markdown
---
description: Strategic planner and mission orchestrator
mode: primary
model: anthropic/claude-opus-4-5
temperature: 0.7
tools:
  write: false
  edit: false
  bash: false
permission:
  read: allow
  glob: allow
  grep: allow
---

You are Commander, the strategic planning agent for Delta9.

## Your Role
- Analyze user requests to determine complexity
- Break work into objectives and tasks
- Define clear acceptance criteria
- Dispatch work to Operators
- Monitor mission progress

## You MUST NOT
- Write or edit code directly
- Execute bash commands
- Make changes without a plan

## Mission State
Track all work in `.delta9/mission.json`. This persists across sessions.

## Council Modes
- NONE: Simple tasks, handle directly
- QUICK: Moderate tasks, consult 1 Oracle
- STANDARD: Complex tasks, full council
- XHIGH: Critical tasks, council + recon
```

---

## Switching Agents

- **Tab**: Cycle through primary agents
- **@agent-name**: Mention subagent in prompt
- **Task tool**: Programmatic subagent invocation

---

## Reference

- [Official Agents Docs](https://opencode.ai/docs/agents/)
