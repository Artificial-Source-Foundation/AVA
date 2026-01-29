# OpenCode MCP Servers

> MCP (Model Context Protocol) enables integration of external tools into OpenCode.

---

## Overview

MCP servers provide additional tools beyond OpenCode's built-ins. Tools from MCP servers are automatically available to the LLM and prefixed with the server name (e.g., `sentry_list`, `context7_search`).

---

## Configuration

Configure in `opencode.json` under the `mcp` key:

```json
{
  "mcp": {
    "server-name": {
      "type": "local",
      "enabled": true,
      "command": ["npx", "-y", "@modelcontextprotocol/server-name"]
    }
  }
}
```

---

## Local MCP Servers

Run MCP servers locally via command execution:

```json
{
  "mcp": {
    "everything": {
      "type": "local",
      "enabled": true,
      "command": ["npx", "-y", "@modelcontextprotocol/server-everything"],
      "environment": {
        "API_KEY": "{env:MY_API_KEY}"
      },
      "timeout": 5000
    }
  }
}
```

| Option | Type | Description |
|--------|------|-------------|
| `type` | `"local"` | Required for local servers |
| `command` | string[] | Executable and arguments |
| `environment` | object | Environment variables |
| `timeout` | number | Response timeout (ms), default 5000 |
| `enabled` | boolean | Activate/deactivate server |

---

## Remote MCP Servers

Connect to hosted MCP endpoints via HTTP:

```json
{
  "mcp": {
    "my-remote": {
      "type": "remote",
      "enabled": true,
      "url": "https://mcp.example.com",
      "headers": {
        "Authorization": "Bearer {env:API_KEY}"
      },
      "timeout": 5000
    }
  }
}
```

| Option | Type | Description |
|--------|------|-------------|
| `type` | `"remote"` | Required for remote servers |
| `url` | string | Remote server endpoint |
| `headers` | object | HTTP headers (API keys, etc.) |
| `oauth` | object | OAuth configuration |
| `timeout` | number | Response timeout (ms) |

---

## OAuth Authentication

OpenCode handles OAuth flows automatically.

### Automatic (Recommended)

```json
{
  "mcp": {
    "sentry": {
      "type": "remote",
      "url": "https://mcp.sentry.dev/mcp",
      "oauth": {}
    }
  }
}
```

Prompts user on first use.

### Pre-registered Credentials

```json
{
  "mcp": {
    "my-server": {
      "type": "remote",
      "url": "https://mcp.example.com",
      "oauth": {
        "clientId": "{env:CLIENT_ID}",
        "clientSecret": "{env:CLIENT_SECRET}",
        "scope": "tools:read tools:execute"
      }
    }
  }
}
```

### Disable OAuth

For API-key-only servers:

```json
{
  "oauth": false
}
```

---

## OAuth Management Commands

```bash
# Authenticate with a server
opencode mcp auth <server-name>

# List all servers and auth status
opencode mcp list

# Clear stored credentials
opencode mcp logout <server-name>

# Debug connection issues
opencode mcp debug <server-name>
```

---

## Tool Control

MCP tools use the same permission system as built-in tools.

### Disable Globally, Enable Per-Agent

```json
{
  "tools": {
    "sentry_*": false
  },
  "agent": {
    "debug-agent": {
      "tools": {
        "sentry_*": true
      }
    }
  }
}
```

### Permission Control

```json
{
  "permission": {
    "github_*": "ask",
    "context7_*": "allow"
  }
}
```

---

## Popular MCP Servers

| Server | Purpose | Configuration |
|--------|---------|---------------|
| **Sentry** | Issue tracking | Remote + OAuth |
| **Context7** | Documentation search | Remote (optional API key) |
| **grep.app** | GitHub code search | Remote |
| **Everything** | File search | Local |

### Sentry Example

```json
{
  "mcp": {
    "sentry": {
      "type": "remote",
      "url": "https://mcp.sentry.dev/mcp",
      "oauth": {}
    }
  }
}
```

### Context7 Example

```json
{
  "mcp": {
    "context7": {
      "type": "remote",
      "url": "https://mcp.context7.io",
      "headers": {
        "Authorization": "Bearer {env:CONTEXT7_API_KEY}"
      }
    }
  }
}
```

---

## Context Usage Warning

Each MCP server adds tokens to your context window. Some servers (like GitHub) can consume significant tokens and exceed context limits.

**Best Practices**:
- Enable only necessary servers
- Monitor token usage
- Disable unused servers with `"enabled": false`

---

## Environment Variables

Use `{env:VAR_NAME}` syntax for secure credential handling:

```json
{
  "mcp": {
    "my-server": {
      "environment": {
        "API_KEY": "{env:MY_API_KEY}",
        "SECRET": "{env:MY_SECRET}"
      }
    }
  }
}
```

---

## Reference

- [Official MCP Docs](https://opencode.ai/docs/mcp-servers/)
- [Model Context Protocol](https://modelcontextprotocol.io/)
