# Feature Gap Analysis: Estela vs SOTA Reference Implementations

> Comprehensive analysis based on 16 subagent deep-dives into OpenCode (70k+⭐), Gemini CLI (50k+⭐), Goose (15k+⭐), Aider, and Plandex

---

## Executive Summary

Estela has solid foundations (platform abstraction, tool execution, OAuth auth, ACP protocol) but is missing several critical features found in mature AI coding assistants. This analysis prioritizes features by **impact** and **effort**.

**Current Estela Score: 6.5/10** (Strong foundation, missing production features)
**Target Score: 9.0/10** (Feature parity with OpenCode/Gemini CLI)

---

## Priority Matrix

| Priority | Feature | Impact | Effort | Reference |
|----------|---------|--------|--------|-----------|
| **P0 CRITICAL** | Permission System | 🔴 High | Medium | Goose, OpenCode |
| **P0 CRITICAL** | PTY Allocation | 🔴 High | High | Gemini CLI |
| **P0 CRITICAL** | Multi-part Messages | 🔴 High | Low | OpenCode |
| **P0 CRITICAL** | Context Compaction | 🔴 High | Medium | Goose, Aider |
| **P1 HIGH** | Tool.define() Wrapper | 🟠 High | Low | OpenCode |
| **P1 HIGH** | Session Persistence | 🟠 High | Medium | Goose |
| **P1 HIGH** | SIGKILL Escalation | 🟠 Medium | Low | Gemini CLI |
| **P1 HIGH** | File Locking | 🟠 Medium | Low | OpenCode |
| **P1 HIGH** | Model Registry | 🟠 Medium | Medium | OpenCode |
| **P2 MEDIUM** | MCP Integration | 🟡 Medium | High | All |
| **P2 MEDIUM** | Sub-agent Spawning | 🟡 Medium | High | Goose |
| **P2 MEDIUM** | Git Snapshots/Undo | 🟡 Medium | Medium | Plandex, Aider |
| **P2 MEDIUM** | Diff-based Edits | 🟡 Medium | Medium | OpenCode, Aider |
| **P3 LOW** | Tree-sitter Parsing | 🟢 Low | High | OpenCode |
| **P3 LOW** | TOML Configs | 🟢 Low | Low | Gemini CLI |
| **P3 LOW** | Streaming Metadata | 🟢 Low | Low | OpenCode |

---

## P0: Critical Missing Features

### 1. Permission System
**What:** User approval for destructive operations (file delete, bash commands, git operations)
**Why Missing:** No user interaction layer in ACP mode
**Reference:** Goose (AlwaysAllow/Deny persistence), OpenCode (sequential permission queue)

**Implementation:**
```
packages/core/src/permissions/
├── types.ts          # Permission request/response types
├── manager.ts        # Permission state, persistence
├── rules.ts          # Built-in rules (never delete .git, etc.)
└── index.ts
```

**Key Features:**
- `PermissionRequest` type with reason, affected paths
- `PermissionRule` pattern matching (glob patterns)
- Session rules (temporary) vs Persistent rules (saved)
- Sequential queue to prevent race conditions
- ACP notification for permission prompts

**Effort:** ~400 lines | **Impact:** Prevents data loss, required for production use

---

### 2. PTY Allocation (Interactive Shell)
**What:** Full terminal emulation for interactive commands (ssh, vim, git rebase -i)
**Why Missing:** Current shell uses spawn() without PTY
**Reference:** Gemini CLI (ptyprocess), OpenCode (node-pty)

**Implementation:**
```typescript
// packages/platform-node/src/pty.ts
import { spawn } from 'node-pty'

export class NodePTY implements IPTY {
  spawn(command: string, options: PTYOptions): PTYProcess {
    const pty = spawn(shell, ['-c', command], {
      name: 'xterm-256color',
      cols: options.cols ?? 80,
      rows: options.rows ?? 24,
      cwd: options.cwd,
      env: options.env,
    })
    // Return unified interface
  }
}
```

**Key Features:**
- Window resize support (SIGWINCH)
- Raw mode for password prompts
- ANSI escape sequence handling
- Fallback to regular spawn for non-interactive

**Effort:** ~500 lines + node-pty dep | **Impact:** Enables interactive workflows

---

### 3. Multi-part Messages
**What:** Messages with mixed content types (text, tool_use, tool_result)
**Why Missing:** Current Message type only supports string content
**Reference:** OpenCode (content blocks array)

**Implementation:**
```typescript
// packages/core/src/types/message.ts
export type ContentBlock =
  | { type: 'text'; text: string }
  | { type: 'tool_use'; id: string; name: string; input: unknown }
  | { type: 'tool_result'; tool_use_id: string; content: string; is_error?: boolean }
  | { type: 'image'; source: ImageSource }

export interface Message {
  id: string
  role: 'user' | 'assistant' | 'system'
  content: string | ContentBlock[]  // Support both
  // ...
}
```

**Effort:** ~150 lines | **Impact:** Enables proper tool conversation history

---

### 4. Context Compaction
**What:** Summarize/truncate conversation when approaching token limit
**Why Missing:** No token tracking or summarization
**Reference:** Goose (conversation compaction), Aider (repo map)

**Implementation:**
```
packages/core/src/context/
├── tracker.ts        # Token counting per message
├── compactor.ts      # Summarization strategies
├── strategies/
│   ├── sliding-window.ts    # Keep last N messages
│   ├── summarize.ts         # LLM summarization
│   └── hierarchical.ts      # Tree structure (Goose)
└── index.ts
```

**Key Features:**
- Token counting using tiktoken or cl100k_base
- Configurable compaction threshold (e.g., 80% of context)
- Multiple strategies (sliding window, summarization, hybrid)
- System message preservation
- Tool result truncation

**Effort:** ~600 lines | **Impact:** Enables long conversations without errors

---

## P1: High Priority Features

### 5. Tool.define() Wrapper
**What:** Declarative tool definition with validation and metadata
**Why Missing:** Current tools use manual schema definition
**Reference:** OpenCode (Zod integration)

**Implementation:**
```typescript
// packages/core/src/tools/define.ts
import { z } from 'zod'

export function defineTool<T extends z.ZodType>(config: {
  name: string
  description: string
  schema: T
  execute: (input: z.infer<T>, ctx: ToolContext) => Promise<ToolResult>
  permissions?: string[]
  locations?: (input: z.infer<T>) => string[]
}): Tool {
  return {
    definition: {
      name: config.name,
      description: config.description,
      input_schema: zodToJsonSchema(config.schema),
    },
    execute: async (input, ctx) => {
      const parsed = config.schema.safeParse(input)
      if (!parsed.success) {
        return { success: false, output: formatZodError(parsed.error) }
      }
      return config.execute(parsed.data, ctx)
    },
  }
}
```

**Effort:** ~200 lines + zod dep | **Impact:** Cleaner tool definitions, better validation

---

### 6. Session Persistence
**What:** Save/restore full session state including tool context
**Why Missing:** Sessions only persist messages, not working state
**Reference:** Goose (session checkpoints)

**Implementation:**
```typescript
// packages/core/src/session/
export interface SessionState {
  id: string
  messages: Message[]
  workingDirectory: string
  toolCallCount: number
  files: Map<string, FileState>  // Opened files, edits
  env: Record<string, string>
  checkpoint?: Checkpoint
}

export interface Checkpoint {
  id: string
  timestamp: number
  git_sha?: string
  description: string
}
```

**Effort:** ~300 lines | **Impact:** Resume interrupted sessions

---

### 7. SIGKILL Escalation
**What:** Force kill processes that ignore SIGTERM
**Why Missing:** Current kill() only sends SIGTERM
**Reference:** Gemini CLI (SIGTERM → wait → SIGKILL)

**Implementation:**
```typescript
// packages/platform-node/src/shell.ts
async kill(signal: NodeJS.Signals = 'SIGTERM', forceAfter = 5000): Promise<void> {
  if (options?.killProcessGroup && process.platform !== 'win32' && child.pid) {
    process.kill(-child.pid, signal)

    // Wait for graceful exit
    const graceful = await Promise.race([
      new Promise(r => child.on('exit', r)),
      new Promise(r => setTimeout(r, forceAfter)),
    ])

    // Force kill if still running
    if (child.exitCode === null) {
      process.kill(-child.pid, 'SIGKILL')
    }
  }
}
```

**Effort:** ~50 lines | **Impact:** Prevents zombie processes

---

### 8. File Locking
**What:** Prevent concurrent edits to same file
**Why Missing:** No coordination between tool calls
**Reference:** OpenCode (file locks)

**Implementation:**
```typescript
// packages/core/src/tools/locks.ts
const fileLocks = new Map<string, Promise<void>>()

export async function withFileLock<T>(
  path: string,
  fn: () => Promise<T>
): Promise<T> {
  const prev = fileLocks.get(path) ?? Promise.resolve()
  const next = prev.then(fn).finally(() => {
    if (fileLocks.get(path) === next) fileLocks.delete(path)
  })
  fileLocks.set(path, next.then(() => {}))
  return next
}
```

**Effort:** ~80 lines | **Impact:** Prevents race conditions in edits

---

### 9. Model Registry
**What:** Centralized model configuration with capabilities
**Why Missing:** Models hardcoded in bridge.ts
**Reference:** OpenCode (models.go)

**Implementation:**
```typescript
// packages/core/src/models/registry.ts
export interface ModelConfig {
  id: string
  provider: LLMProvider
  displayName: string
  contextWindow: number
  maxOutputTokens: number
  capabilities: {
    tools: boolean
    vision: boolean
    streaming: boolean
  }
  pricing?: { input: number; output: number }
}

export const MODEL_REGISTRY: Record<string, ModelConfig> = {
  'claude-sonnet-4': {
    id: 'claude-sonnet-4-20250514',
    provider: 'anthropic',
    displayName: 'Claude Sonnet 4',
    contextWindow: 200000,
    maxOutputTokens: 16384,
    capabilities: { tools: true, vision: true, streaming: true },
  },
  // ...
}
```

**Effort:** ~200 lines | **Impact:** Centralized model management, capability checking

---

## P2: Medium Priority Features

### 10. MCP (Model Context Protocol) Integration
**What:** Connect to external tools via MCP servers
**Why Missing:** No MCP client implementation
**Reference:** All modern assistants (Claude Code, Cursor, etc.)

**Implementation:**
```
packages/core/src/mcp/
├── client.ts         # MCP client implementation
├── discovery.ts      # Auto-discover installed servers
├── bridge.ts         # Expose MCP tools to LLM
└── servers/          # Built-in server stubs
```

**Effort:** ~800 lines | **Impact:** Extensibility via MCP ecosystem

---

### 11. Sub-agent Spawning
**What:** Delegate subtasks to specialized agents
**Why Missing:** Single-agent architecture
**Reference:** Goose (multi-layer dispatch)

**Implementation:**
```typescript
// packages/core/src/agents/
export interface AgentConfig {
  name: string
  role: string
  tools: string[]
  systemPrompt: string
}

export async function spawnSubagent(
  config: AgentConfig,
  task: string,
  ctx: ToolContext
): Promise<string> {
  // Create child session
  // Run with limited tool set
  // Return result to parent
}
```

**Effort:** ~500 lines | **Impact:** Complex task decomposition

---

### 12. Git Snapshots/Undo
**What:** Create git commits before changes for easy rollback
**Why Missing:** No git integration
**Reference:** Plandex (plan/branch model), Aider (auto-commits)

**Implementation:**
```typescript
// packages/core/src/git/
export async function createSnapshot(
  message: string,
  paths: string[]
): Promise<string> {
  // git stash or commit
  // Return SHA for rollback
}

export async function rollback(sha: string): Promise<void> {
  // git checkout or reset
}
```

**Effort:** ~300 lines | **Impact:** Safety net for all changes

---

### 13. Diff-based Edits
**What:** Track changes as unified diffs for review
**Why Missing:** Current edits are direct file writes
**Reference:** OpenCode, Aider (unified diffs)

**Implementation:**
```typescript
// packages/core/src/diff/
export interface PendingEdit {
  path: string
  original: string
  modified: string
  diff: string  // Unified diff
  status: 'pending' | 'applied' | 'rejected'
}

export function createDiff(original: string, modified: string): string {
  // Use diff library
}
```

**Effort:** ~250 lines | **Impact:** Better code review, undo support

---

## P3: Low Priority Features

### 14. Tree-sitter Command Parsing
**What:** Parse bash commands for pre-flight permission detection
**Why Missing:** Commands treated as opaque strings
**Reference:** OpenCode

**Effort:** ~400 lines + tree-sitter dep | **Impact:** Smarter permission prompts

### 15. TOML Configuration
**What:** Support TOML config files alongside JSON
**Why Missing:** Only JSON/env config currently
**Reference:** Gemini CLI

**Effort:** ~100 lines + toml dep | **Impact:** User preference

### 16. Streaming Metadata
**What:** Send metadata during tool execution for UI updates
**Why Missing:** Tools return final result only
**Reference:** OpenCode (ctx.metadata())

**Effort:** ~150 lines | **Impact:** Better UX during long operations

---

## Implementation Roadmap

### Phase 1: Safety & Stability (2 weeks)
```
[ ] P0: Permission System
[ ] P1: SIGKILL Escalation
[ ] P1: File Locking
[ ] P0: Multi-part Messages
```

### Phase 2: Long Conversations (2 weeks)
```
[ ] P0: Context Compaction
[ ] P1: Session Persistence
[ ] P1: Model Registry
```

### Phase 3: Developer Experience (2 weeks)
```
[ ] P1: Tool.define() Wrapper
[ ] P2: Diff-based Edits
[ ] P2: Git Snapshots/Undo
```

### Phase 4: Advanced Features (3 weeks)
```
[ ] P0: PTY Allocation
[ ] P2: MCP Integration
[ ] P2: Sub-agent Spawning
```

### Phase 5: Polish (1 week)
```
[ ] P3: Streaming Metadata
[ ] P3: TOML Configuration
[ ] P3: Tree-sitter Parsing (optional)
```

---

## Effort Summary

| Phase | Features | Estimated Lines | Weeks |
|-------|----------|-----------------|-------|
| Phase 1 | 4 | ~700 | 2 |
| Phase 2 | 3 | ~1100 | 2 |
| Phase 3 | 3 | ~750 | 2 |
| Phase 4 | 3 | ~1800 | 3 |
| Phase 5 | 3 | ~650 | 1 |
| **Total** | **16** | **~5000** | **10** |

---

## Architecture After Implementation

```
packages/core/src/
├── agents/           # NEW: Sub-agent spawning
├── auth/             # Existing: OAuth, API keys
├── context/          # NEW: Token tracking, compaction
├── diff/             # NEW: Unified diffs, pending edits
├── git/              # NEW: Snapshots, rollback
├── llm/              # Existing: Provider clients
├── mcp/              # NEW: MCP client integration
├── models/           # NEW: Model registry
├── permissions/      # NEW: Permission system
├── platform.ts       # Existing: Platform abstraction
├── session/          # NEW: Full session state
├── tools/
│   ├── define.ts     # NEW: Tool.define() wrapper
│   ├── locks.ts      # NEW: File locking
│   └── ...           # Existing tools
└── types/            # Existing + multi-part messages

packages/platform-node/src/
├── fs.ts             # Existing
├── shell.ts          # Existing + SIGKILL escalation
├── pty.ts            # NEW: PTY allocation
└── ...
```

---

## References

| Project | Stars | Key Learnings |
|---------|-------|---------------|
| OpenCode | 70k+ | Tool.define(), file locking, model registry, diff edits |
| Gemini CLI | 50k+ | PTY allocation, SIGKILL escalation, TOML config |
| Goose | 15k+ | Permission manager, context compaction, session LRU |
| Aider | 25k+ | Git integration, unified diffs, repo map |
| Plandex | 8k+ | Plan/branch model, snapshot undo |

---

## Decision Log

| Decision | Rationale |
|----------|-----------|
| Permission System as P0 | Required for production - prevents data loss |
| PTY in Phase 4 | High effort, most users don't need interactive shell |
| MCP in Phase 4 | Valuable but complex, ecosystem still evolving |
| Skip Tree-sitter | High effort, low impact for permission detection |

---

*Generated: 2026-02-02*
*Based on: 16 subagent analyses of reference implementations*
