# Gemini CLI vs AVA: Comparison & Feature Gaps

> Comprehensive comparison based on full codebase analysis of Gemini CLI

---

## Executive Summary

Gemini CLI is a production-grade AI coding assistant built on React + Ink for terminal UI. After analyzing all 5 packages (~150+ files), several architectural patterns and features could enhance AVA.

**Key Findings:**
- Gemini CLI has **more mature infrastructure** (policy engine, message bus, extension system)
- AVA has **unique strengths** (browser automation, Tauri desktop, SolidJS)
- Several **high-value features** are missing from AVA

---

## Analysis Documents

| Document | Size | Focus |
|----------|------|-------|
| [root-configs.md](./root-configs.md) | ~14KB | Monorepo structure, build system, CI/CD |
| [vscode-extension.md](./vscode-extension.md) | ~23KB | IDE integration, MCP discovery, 20+ IDEs |
| [a2a-server.md](./a2a-server.md) | ~27KB | Agent-to-Agent protocol, REST API, agent cards |
| [core.md](./core.md) | ~40KB | Tools, hooks, policy engine, compression |
| [cli.md](./cli.md) | ~35KB | TUI, commands, services, extensions |

**Total documentation: ~139KB**

---

## Architecture Comparison

| Aspect | Gemini CLI | AVA |
|--------|------------|--------|
| **UI Framework** | React + Ink (terminal) | SolidJS + Tauri (desktop) |
| **Runtime** | Node.js CLI | Tauri (Rust + Web) |
| **Tool Approval** | Message Bus + Policy Engine | Direct confirmation |
| **Hooks** | Command-based (shell scripts) | TypeScript functions |
| **Context Management** | LLM summarization compression | Token tracking + compaction |
| **Shell Execution** | PTY with xterm (300K buffer) | Basic spawn |
| **MCP Auth** | Full OAuth 2.0 + PKCE | Basic token |
| **Extension System** | GitHub/local/link extensions | None (planned) |
| **Settings** | 5-scope system (user→workspace) | Single settings file |

---

## Feature Gap Analysis

### Critical Gaps (High Impact)

| Feature | Gemini CLI | AVA | Priority |
|---------|------------|--------|----------|
| **Policy Engine** | Priority-based rules, wildcards, regex | Simple auto-approve list | 🔴 High |
| **Message Bus** | Decoupled tool/UI, correlation IDs | Tight coupling | 🔴 High |
| **Extension System** | GitHub, local, linked extensions | None | 🔴 High |
| **Session Resume** | Full resume, browser UI, metadata | None | 🔴 High |

### Important Gaps (Medium Impact)

| Feature | Gemini CLI | AVA | Priority |
|---------|------------|--------|----------|
| **Chat Compression** | LLM summarization with verification | Basic token compaction | 🟡 Medium |
| **PTY Shell** | xterm with 300K scrollback | Basic spawn | 🟡 Medium |
| **Model Availability** | Terminal vs sticky failures, fallback | No fallback | 🟡 Medium |
| **TOML Commands** | Custom user commands | None | 🟡 Medium |
| **Trusted Folders** | Security boundaries | None | 🟡 Medium |
| **Loop Detection** | Automatic repetition detection | None | 🟡 Medium |

### Nice-to-Have Gaps (Lower Impact)

| Feature | Gemini CLI | AVA | Priority |
|---------|------------|--------|----------|
| **A2A Protocol** | Multi-agent REST API | None | 🟢 Low |
| **Agent Cards** | `.well-known/agent-card.json` | None | 🟢 Low |
| **IDE Detection** | 20+ IDEs auto-detected | None | 🟢 Low |
| **Sandbox Mode** | Docker/Podman/sandbox-exec | None | 🟢 Low |

---

## What AVA Does Better

| Feature | AVA Advantage |
|---------|------------------|
| **Browser Automation** | Built-in Puppeteer tool, not in Gemini CLI |
| **Desktop App** | Native Tauri app with SolidJS |
| **Fuzzy Edit** | 8 edit strategies vs Gemini's basic |
| **Batch Tool** | 25 parallel tool calls |
| **Multi-Edit** | Atomic multi-edit in single call |
| **Apply Patch** | Unified diff format support |
| **Code Search** | Exa API integration |
| **Skill System** | Markdown-based knowledge modules |
| **Plan Mode** | Built-in planning with read-only tools |

---

## Recommended Priorities

### Sprint 1: Infrastructure Foundation

**1. Policy Engine** (~800 lines)
```typescript
// Key features to implement:
interface PolicyRule {
  name: string;
  toolName?: string;        // Wildcard support: 'shell', '*', 'mcp__*'
  argsPattern?: RegExp;     // Regex matching on args
  decision: 'allow' | 'deny' | 'ask_user';
  priority: number;         // Higher = checked first
  modes?: ApprovalMode[];   // When rule applies
}
```

**2. Message Bus** (~500 lines)
```typescript
// Decouple tool execution from UI
interface MessageBus {
  publish(message: Message): Promise<void>;
  subscribe(type: MessageType, handler: Handler): void;
  request<T, R>(request: T, responseType: string, timeout: number): Promise<R>;
}
```

### Sprint 2: User Experience

**3. Session Resume** (~600 lines)
- Save session state to disk
- Resume by ID or "latest"
- Session browser UI
- Metadata: summary, message count, timestamps

**4. TOML Custom Commands** (~400 lines)
```toml
# ~/.ava/commands/deploy.toml
description = "Deploy to production"
prompt = """
Run the deployment script:
$(cat deploy.sh)
"""
```

### Sprint 3: Extensibility

**5. Extension System** (~1,200 lines)
- Extension manifest format
- Install from: GitHub, local, link
- Extension capabilities: commands, MCP servers, skills, themes

**6. Trusted Folders** (~300 lines)
- Per-folder trust levels
- Security boundaries
- IDE trust integration

### Sprint 4: Intelligence

**7. Chat Compression** (~600 lines)
- Token threshold detection (50% of limit)
- LLM summarization with `<state_snapshot>`
- Self-correction verification pass

**8. Loop Detection** (~200 lines)
- Track last N tool calls
- Detect 3+ identical consecutive calls
- User prompt before continuing

---

## Implementation Notes

### Policy Engine Design

Gemini CLI's policy engine uses this decision flow:
```
Tool Call → Check Rules (by priority) →
  ALLOW → Execute
  DENY → Error message
  ASK_USER → Show confirmation UI

For shell commands:
  Parse compound commands (pipes, chains) →
  Check each subcommand recursively →
  Downgrade ALLOW → ASK_USER for redirections
```

### Message Bus Pattern

```typescript
// Example usage
const response = await messageBus.request<
  ToolConfirmationRequest,
  ToolConfirmationResponse
>(
  {
    type: 'TOOL_CONFIRMATION_REQUEST',
    toolCall: { name: 'shell', args: { command: 'rm -rf /' } },
    details: { type: 'exec', command: 'rm -rf /', rootCommand: 'rm' },
  },
  'TOOL_CONFIRMATION_RESPONSE',
  60000 // timeout
);
```

### Hook System Enhancement

Gemini CLI's hooks are command-based (shell scripts):
```json
{
  "hooks": {
    "BeforeTool": [{
      "type": "command",
      "command": "python3 ~/.gemini/hooks/security.py",
      "timeout": 5000
    }]
  }
}
```

Exit codes control behavior:
- 0 = Allow
- 1 = Warning (continue with notice)
- 2 = Block (stop execution)

---

## Code Statistics

| Component | Gemini CLI | AVA |
|-----------|------------|--------|
| Core Package | ~25K lines | ~25K lines |
| CLI/Frontend | ~15K lines | ~5K lines |
| Extensions | ~2K lines | 0 |
| A2A Server | ~3K lines | 0 |
| VS Code | ~5K lines | 0 |
| **Total** | **~50K lines** | **~30K lines** |

---

## Next Steps

1. **Immediate**: Implement Policy Engine for better approval management
2. **Short-term**: Add Message Bus to decouple UI from tools
3. **Medium-term**: Session resume and TOML commands
4. **Long-term**: Full extension system with marketplace

---

## Reference Files

Key files to study in Gemini CLI:

| Feature | File Path |
|---------|-----------|
| Policy Engine | `packages/core/src/policy/policy-engine.ts` |
| Message Bus | `packages/core/src/confirmation-bus/message-bus.ts` |
| Hook System | `packages/core/src/hooks/hookRunner.ts` |
| Compression | `packages/core/src/services/chatCompressionService.ts` |
| Tool Registry | `packages/core/src/tools/tool-registry.ts` |
| Session Utils | `packages/cli/src/utils/sessionUtils.ts` |
| Command Service | `packages/cli/src/services/CommandService.ts` |
| Extension Manager | `packages/cli/src/config/extension-manager.ts` |

---

*Analysis completed: 2026-02-04*
*Gemini CLI version analyzed: 0.28.0-nightly*
