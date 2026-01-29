# OpenCode Overview

> OpenCode is an AI-native terminal-based code editor that integrates multiple LLM providers for intelligent code assistance.

---

## What is OpenCode?

OpenCode is a terminal UI (TUI) application that provides:
- Multi-provider LLM integration (75+ providers via AI SDK)
- Agent-based architecture with primary and subagents
- Plugin system for extensibility
- MCP (Model Context Protocol) server support
- Built-in tools for file operations, search, and shell access

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                    OpenCode TUI                      │
│              (Terminal User Interface)               │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│                   OpenCode Server                    │
│           (OpenAPI 3.1 HTTP Server)                  │
│                                                      │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐            │
│  │ Sessions │  │ Agents  │  │  Tools  │            │
│  └─────────┘  └─────────┘  └─────────┘            │
│                                                      │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐            │
│  │ Plugins │  │   MCP   │  │  Skills │            │
│  └─────────┘  └─────────┘  └─────────┘            │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│                  LLM Providers                       │
│  Anthropic | OpenAI | Google | DeepSeek | etc.      │
└─────────────────────────────────────────────────────┘
```

---

## Key Components

| Component | Description |
|-----------|-------------|
| **Agents** | Primary (main assistants) and subagents (specialists) |
| **Tools** | 14 built-in tools + custom tools via plugins |
| **Plugins** | Extend functionality with hooks, tools, agents |
| **MCP Servers** | External tool integration via Model Context Protocol |
| **Skills** | Reusable instruction sets in SKILL.md files |
| **Commands** | Custom slash commands in markdown format |

---

## Configuration Locations

| Location | Purpose |
|----------|---------|
| `~/.config/opencode/opencode.json` | Global config |
| `opencode.json` | Project config |
| `.opencode/` | Project plugins, tools, commands, agents |
| `~/.config/opencode/` | Global plugins, tools, commands, agents |

---

## Requirements

- **Runtime**: Bun (for npm plugin loading), Node.js 18+ (fallback)
- **OpenCode Version**: 1.0.150+ recommended

---

## Quick Start

```bash
# Install OpenCode
npm install -g opencode

# Start in project directory
cd your-project
opencode

# Configure providers
/connect anthropic
/connect openai
```

---

## Official Documentation

- [OpenCode Docs](https://opencode.ai/docs/)
- [Plugins](https://opencode.ai/docs/plugins/)
- [Agents](https://opencode.ai/docs/agents/)
- [Tools](https://opencode.ai/docs/tools/)
- [SDK](https://opencode.ai/docs/sdk/)
