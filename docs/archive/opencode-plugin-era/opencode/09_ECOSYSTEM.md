# OpenCode Ecosystem

> Comprehensive guide to OpenCode plugins, tools, and community resources.

---

## Official Resources

### Core Repositories

| Repository | Description |
|------------|-------------|
| [opencode](https://github.com/opencode-ai/opencode) | Official CLI agent |
| [opencode-sdk-js](https://github.com/opencode-ai/opencode-sdk-js) | JavaScript/TypeScript SDK |
| [opencode-sdk-go](https://github.com/opencode-ai/opencode-sdk-go) | Go SDK |
| [opencode-sdk-python](https://github.com/opencode-ai/opencode-sdk-python) | Python SDK |

### Documentation

- [OpenCode Docs](https://opencode.ai/docs/)
- [Plugin Development](https://opencode.ai/docs/plugins/)
- [Agent Configuration](https://opencode.ai/docs/agents/)
- [Model Providers](https://opencode.ai/docs/providers/)

---

## Plugin Categories

### Agent Orchestration

| Plugin | Description | Key Features |
|--------|-------------|--------------|
| **oh-my-opencode** | Gold standard orchestrator | Background agents, 10+ specialists, 31 hooks |
| **oh-my-opencode-slim** | Lightweight fork | Token-efficient, minimal overhead |
| **Workspace** | 16-component bundle | Multi-agent coordination |
| **Swarm Plugin** | Swarm coordination | Agent clustering |
| **Pocket Universe** | Closed-loop subagents | Reliable async execution |
| **Subtask2** | Orchestration framework | Flow control, task decomposition |

### Memory & Context

| Plugin | Description | Storage |
|--------|-------------|---------|
| **Agent Memory** | Letta-inspired blocks | Persistent memory |
| **Opencode Mem** | Vector database context | Long-term retention |
| **Simple Memory** | Git-based memory | Team collaboration |
| **Context Analysis** | Token monitoring | Usage analytics |

### Authentication

| Plugin | Description | Provider |
|--------|-------------|----------|
| **Antigravity Auth** | OAuth integration | Google Gemini/Anthropic |
| **Antigravity Multi-Auth** | Account rotation | Multiple Google accounts |
| **Gemini Auth** | Google OAuth | Gemini models |
| **OpenAI Codex Auth** | ChatGPT Plus/Pro | OpenAI via browser |

### Workflow Automation

| Plugin | Description | Use Case |
|--------|-------------|----------|
| **Pilot** | GitHub/Linear automation | Issue tracking |
| **Worktree** | Git worktree automation | Branch management |
| **Froggy** | Hooks + gitingest | Code analysis |
| **Ralph Wiggum** | Self-correcting loops | Autonomous operation |

### Development Tools

| Plugin | Description | Capability |
|--------|-------------|------------|
| **Devcontainers** | Container isolation | Multi-branch |
| **Direnv** | Environment loading | Nix flakes |
| **Shell Strategy** | Non-interactive shell | CI/CD guidance |
| **Morph Fast Apply** | Fast code editing | 10,500+ tokens/sec |

### UI & Visualization

| Plugin | Description | Feature |
|--------|-------------|---------|
| **Opencode Canvas** | Terminal canvases | tmux integration |
| **Plannotator** | Plan review | Visual annotation |
| **Smart Title** | Session naming | Auto-generated titles |

### Notifications

| Plugin | Description | Integration |
|--------|-------------|-------------|
| **Opencode Notify** | Native OS notifications | Desktop alerts |
| **Smart Voice Notify** | TTS notifications | ElevenLabs, Edge TTS |
| **Opencode Mystatus** | Quota checking | Subscription status |

### Code Quality

| Plugin | Description | Protection |
|--------|-------------|------------|
| **CC Safety Net** | Destructive command guard | Git/filesystem |
| **Envsitter Guard** | .env leak prevention | Secrets |
| **Opencode Ignore** | File filtering | Pattern-based |
| **Dynamic Context Pruning** | Token optimization | Context management |

### Productivity

| Plugin | Description |
|--------|-------------|
| **Snippets** | Inline text expansion |
| **Handoff** | Session continuation |
| **Beads Plugin** | Issue tracker (Steve Yegge) |
| **Synced** | Config sync across machines |
| **Tokenscope** | Token analysis and cost |
| **WakaTime** | Activity tracking |
| **Micode** | Brainstorm-Plan-Implement |
| **Roadmap** | Strategic planning |

---

## Model Providers

### Cloud Providers

OpenCode integrates with 75+ LLM providers through the AI SDK.

**Major Providers:**
- Anthropic (Claude 3.5, Claude 4, Opus 4.5)
- OpenAI (GPT-4o, GPT-5, Codex)
- Google (Gemini 2.5, Gemini 3)
- xAI (Grok)
- DeepSeek (v3)

### Enterprise Providers

- Amazon Bedrock
- Azure OpenAI
- Google Vertex AI
- SAP AI Core
- GitLab Duo
- GitHub Copilot

### Local Models

- Ollama
- LM Studio
- llama.cpp

### Configuration

```json
{
  "$schema": "https://opencode.ai/config.json",
  "provider": {
    "anthropic": {
      "options": {
        "baseURL": "https://api.anthropic.com/v1"
      }
    },
    "openai": {
      "options": {
        "apiKey": "{env:OPENAI_API_KEY}"
      }
    }
  }
}
```

### Custom Providers

Any OpenAI-compatible endpoint can be added:

```json
{
  "provider": {
    "custom-llm": {
      "options": {
        "baseURL": "https://my-llm-api.com/v1",
        "apiKey": "{env:CUSTOM_API_KEY}"
      }
    }
  }
}
```

---

## Built-in MCP Servers

### Popular MCPs

| MCP | Purpose | Usage |
|-----|---------|-------|
| **Exa** | Real-time web search | Research tasks |
| **Context7** | Library documentation | API lookups |
| **grep.app** | GitHub code search | Pattern finding |
| **Puppeteer** | Browser automation | Testing, scraping |
| **Sequential Thinking** | Step-by-step reasoning | Complex problems |

### MCP Configuration

```json
{
  "mcp": {
    "exa": {
      "command": "npx",
      "args": ["-y", "@anthropic/mcp-exa"],
      "env": {
        "EXA_API_KEY": "{env:EXA_API_KEY}"
      }
    }
  }
}
```

---

## Skills System

### Skill Locations

```
.opencode/skills/          # Project-specific
~/.opencode/skills/        # User-level
~/.config/opencode/skills/ # Config directory
```

### Skill Format (SKILL.md)

```markdown
---
name: my-skill
description: "What this skill does"
allowed-tools:
  - read
  - write
  - bash
---

# My Skill

Instructions for the agent when this skill is loaded...
```

### Loading Skills

```typescript
// Via tool
use_skills({ skill_names: ["playwright", "frontend-ui-ux"] })

// Via delegate_task
delegate_task({
  category: "visual-engineering",
  load_skills: ["playwright", "git-master"],
  prompt: "...",
})
```

---

## Community Resources

### Curated Lists

- [awesome-opencode](https://github.com/awesome-opencode/awesome-opencode) - Plugin directory
- [OpenCode Discord](https://discord.gg/opencode) - Community support

### Reference Implementations

| Repository | Pattern |
|------------|---------|
| oh-my-opencode | Full orchestration |
| opencode-plugin-template | Starter template |
| opencode-skillful | Skills system |
| opencode-plugins | Utility collection |

### Learning Resources

- [OpenCode Blog](https://opencode.ai/blog/)
- [Plugin Development Guide](https://opencode.ai/docs/plugins/)
- [Agent Configuration](https://opencode.ai/docs/agents/)

---

## Delta9 Integration Points

### Plugins to Study

1. **oh-my-opencode** - Orchestration patterns
2. **opencode-skillful** - Skills loading
3. **Agent Memory** - Persistent state patterns
4. **Pocket Universe** - Async subagent coordination

### MCPs to Consider

1. **Exa** - Research for Council
2. **Context7** - Documentation lookup
3. **grep.app** - Code pattern search

### Provider Strategy

For Delta9's heterogeneous Council:
- **Anthropic** - Claude models (Commander, Oracle-Claude)
- **OpenAI** - GPT models (Oracle-GPT)
- **Google** - Gemini models (Oracle-Gemini, UI-Ops)
- **DeepSeek** - Performance specialist (Oracle-DeepSeek)

---

## References

- [awesome-opencode GitHub](https://github.com/awesome-opencode/awesome-opencode)
- [OpenCode Documentation](https://opencode.ai/docs/)
- [oh-my-opencode](https://github.com/code-yeongyu/oh-my-opencode)
- [opencode-skillful](https://github.com/...)
