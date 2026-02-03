# Epic 3: ACP + Core Monorepo

> ✅ Completed: 2025-02-02

---

## Goal

Create shared core package with platform abstraction, enabling both Tauri desktop and CLI/ACP modes.

---

## Sprints Completed

| Sprint | What | Lines |
|--------|------|-------|
| 3.1 | Monorepo Setup | ~200 |
| 3.2 | Platform Abstraction | ~800 |
| 3.3 | Core Tools Migration | ~1500 |
| 3.4 | OAuth Authentication | ~1180 |
| 3.5 | ACP Agent Integration | ~400 |
| 3.6 | Tauri Integration | ~200 |

**Total:** ~4280 lines (70 files changed, 5260 insertions)

---

## What Was Built

### Monorepo Structure
```
packages/
├── core/              # Shared LLM, auth, types, tools
│   └── src/
│       ├── llm/       # Provider clients
│       ├── auth/      # OAuth + API key management
│       ├── tools/     # Platform-agnostic tools (7)
│       ├── types/     # Shared types
│       └── platform.ts
├── platform-node/     # Node.js implementations
│   └── src/
│       ├── fs.ts      # Node filesystem
│       ├── shell.ts   # Process spawn with group killing
│       ├── credentials.ts
│       └── database.ts
├── platform-tauri/    # Tauri implementations
│   └── src/
│       ├── fs.ts      # Tauri filesystem with proper glob
│       ├── shell.ts
│       └── credentials.ts
└── cli/               # CLI with ACP agent
    └── src/
        ├── index.ts   # Entry point
        ├── commands/  # auth, chat
        └── acp/       # Agent protocol
```

### OAuth Authentication (4 Providers)
- **Anthropic**: PKCE flow for Claude Pro/Max
- **OpenAI**: Device code flow for ChatGPT Plus/Pro
- **Google**: PKCE flow for Gemini/Antigravity
- **GitHub Copilot**: Device code flow

### Platform Abstraction
- `IFileSystem`: readFile, writeFile, readBinary, stat, glob, readDirWithTypes
- `IShell`: exec, spawn with killProcessGroup option
- `ICredentialStore`: get, set, delete, list
- `IDatabase`: query, execute

### SOTA Features Implemented
| Feature | Source | Location |
|---------|--------|----------|
| Process group killing | Gemini CLI | platform-node/shell.ts |
| Binary output detection | Gemini CLI | core/tools/utils.ts |
| Tool location tracking | Goose | core/tools/types.ts |
| Tool call limits | OpenCode | core/tools/registry.ts |

---

## Key Decisions

- **pnpm workspaces**: Monorepo with shared dependencies
- **Platform abstraction**: `getPlatform()` returns current implementation
- **Tools in core**: Single implementation, works everywhere
- **Auto-registration**: Tools register on import via side effects

---

## ACP Protocol Support

```typescript
// JSON-RPC over stdio for Zed/Toad integration
interface ACPMethods {
  'initialize': () => InitializeResult
  'prompt': (params: PromptParams) => AsyncGenerator<SessionUpdate>
  'session/cancel': (params: { sessionId: string }) => void
}

// Tool execution in ACP mode
// Same tools work in CLI and Tauri
const tools = getToolDefinitions()
for (const toolUse of pendingToolUses) {
  const result = await executeTool(toolUse.name, toolUse.input, ctx)
  // Send tool_call and tool_result notifications
}
```

---

## Files Created/Modified

**70 files changed:**
- 15 new files in `packages/core/`
- 8 new files in `packages/platform-node/`
- 6 new files in `packages/platform-tauri/`
- 12 new files in `cli/`
- Various modifications to existing files

---

## Commands

```bash
# CLI usage
estela chat "Hello"              # One-shot chat
estela auth login anthropic      # OAuth login
estela auth status               # Show auth status
estela --acp                     # ACP agent mode for editors

# Development
pnpm build:all                   # Build all packages
pnpm tauri dev                   # Run Tauri app
```
