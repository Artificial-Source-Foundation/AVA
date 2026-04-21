---
title: "MCP Servers"
description: "Configure Model Context Protocol servers so AVA can use external tool backends."
order: 3
updated: "2026-04-08"
---

# MCP Servers

AVA connects to [Model Context Protocol](https://modelcontextprotocol.io/) servers so external tools can be exposed like built-in tools.

## Configuration Files

Create `mcp.json` in either location:

1. `~/.ava/mcp.json` - global servers for all projects
2. `.ava/mcp.json` - project-local servers

Project configs override global configs by server name.

Project-local `.ava/mcp.json` requires workspace trust. Run `ava --trust` to approve a project.

## Basic Format

```json
{
  "servers": [
    {
      "name": "server-name",
      "transport": { "type": "stdio", "command": "npx", "args": ["-y", "some-server"] },
      "enabled": true
    }
  ]
}
```

The `enabled` field defaults to `true` and can be set to `false` without removing the config.

## Transport Types

Stdio example:

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

HTTP example:

```json
{
  "name": "remote-tools",
  "transport": {
    "type": "http",
    "url": "http://localhost:8080"
  }
}
```

## TUI Commands

Use `/mcp` in the TUI:

| Command | Action |
|---|---|
| `/mcp` or `/mcp list` | List configured servers and their status |
| `/mcp reload` | Reload configs and reconnect |
| `/mcp enable <name>` | Enable a server |
| `/mcp disable <name>` | Disable a server |

## Implementation

1. Config loading: `crates/ava-mcp/src/config.rs`
2. Client runtime: `crates/ava-mcp/src/client.rs`
3. Transports: `crates/ava-mcp/src/transport.rs`
