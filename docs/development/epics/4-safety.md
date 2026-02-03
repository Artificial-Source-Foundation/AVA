# Epic 4: Safety & Stability

> Permissions, file locking, process control

---

## Goal

Add safety features required for production use - user approval for destructive operations, race condition prevention, and proper process lifecycle management.

---

## Reference Implementations

| Feature | Source | Stars |
|---------|--------|-------|
| Permission manager | Goose | 15k+ |
| Sequential permission queue | OpenCode | 70k+ |
| SIGKILL escalation | Gemini CLI | 50k+ |
| File locking | OpenCode | 70k+ |

---

## Sprints

| # | Sprint | Tasks | Est. Lines |
|---|--------|-------|------------|
| 4.1 | Permission System | Types, manager, rules, ACP notifications | ~400 |
| 4.2 | Process Control | SIGKILL escalation, timeout improvements | ~100 |
| 4.3 | File Safety | File locking, concurrent edit prevention | ~100 |
| 4.4 | Multi-part Messages | Mixed content blocks in messages | ~150 |

**Total:** ~750 lines

---

## Sprint 4.1: Permission System

### Files to Create

```
packages/core/src/permissions/
├── types.ts          # PermissionRequest, PermissionRule, PermissionDecision
├── manager.ts        # State management, rule matching, persistence
├── rules.ts          # Built-in rules (never delete .git, warn on rm -rf)
└── index.ts          # Barrel export
```

### Key Types

```typescript
export interface PermissionRequest {
  id: string
  tool: string
  action: 'read' | 'write' | 'delete' | 'execute'
  paths: string[]
  reason: string
  risk: 'low' | 'medium' | 'high' | 'critical'
}

export interface PermissionRule {
  pattern: string       // Glob pattern
  action: 'allow' | 'deny' | 'ask'
  scope: 'session' | 'persistent'
}

export type PermissionDecision =
  | { allowed: true }
  | { allowed: false; reason: string }
  | { ask: true; request: PermissionRequest }
```

### ACP Integration

```typescript
// Send permission request to client
sendSessionUpdate(connection, sessionId, {
  sessionUpdate: 'permission_request',
  permissionRequest: { id, tool, paths, reason, risk }
})

// Receive decision
// Handle: 'permission_response' with { id, decision }
```

### Built-in Rules

| Pattern | Action | Reason |
|---------|--------|--------|
| `**/.git/**` | deny | Protect git history |
| `**/.env*` | ask | May contain secrets |
| `**/node_modules/**` | deny | Don't modify dependencies |
| `rm -rf *` | deny | Too dangerous |
| `sudo *` | ask | Elevated privileges |

---

## Sprint 4.2: Process Control

### Files to Modify

**`packages/platform-node/src/shell.ts`**

```typescript
// Add SIGKILL escalation
async kill(signal: NodeJS.Signals = 'SIGTERM', forceAfter = 5000): Promise<void> {
  const pid = this.child.pid
  if (!pid) return

  // Send initial signal
  if (this.options?.killProcessGroup && process.platform !== 'win32') {
    process.kill(-pid, signal)
  } else {
    this.child.kill(signal)
  }

  // Wait for graceful exit
  const exited = await Promise.race([
    new Promise<boolean>(r => this.child.on('exit', () => r(true))),
    new Promise<boolean>(r => setTimeout(() => r(false), forceAfter)),
  ])

  // Force kill if still running
  if (!exited && this.child.exitCode === null) {
    if (this.options?.killProcessGroup && process.platform !== 'win32') {
      process.kill(-pid, 'SIGKILL')
    } else {
      this.child.kill('SIGKILL')
    }
  }
}
```

### Inactivity Timeout

```typescript
// Reset timeout on any output
let timeoutId: NodeJS.Timeout | null = null

const resetTimeout = () => {
  if (timeoutId) clearTimeout(timeoutId)
  if (options?.inactivityTimeout) {
    timeoutId = setTimeout(() => {
      this.kill('SIGTERM', 2000)
    }, options.inactivityTimeout)
  }
}

child.stdout?.on('data', resetTimeout)
child.stderr?.on('data', resetTimeout)
```

---

## Sprint 4.3: File Safety

### Files to Create

**`packages/core/src/tools/locks.ts`**

```typescript
const fileLocks = new Map<string, Promise<void>>()

export async function withFileLock<T>(
  path: string,
  fn: () => Promise<T>
): Promise<T> {
  // Wait for any existing lock
  const prev = fileLocks.get(path) ?? Promise.resolve()

  // Create new lock
  let resolve: () => void
  const lock = new Promise<void>(r => { resolve = r })

  const next = prev.then(async () => {
    try {
      return await fn()
    } finally {
      resolve()
      if (fileLocks.get(path) === lock) {
        fileLocks.delete(path)
      }
    }
  })

  fileLocks.set(path, lock)
  return next
}
```

### Tool Integration

```typescript
// In write.ts, create.ts, delete.ts
import { withFileLock } from './locks.js'

export async function execute(input: WriteInput, ctx: ToolContext): Promise<ToolResult> {
  return withFileLock(input.path, async () => {
    // Existing implementation
  })
}
```

---

## Sprint 4.4: Multi-part Messages

### Files to Modify

**`packages/core/src/types/message.ts`**

```typescript
export type ContentBlock =
  | { type: 'text'; text: string }
  | { type: 'tool_use'; id: string; name: string; input: unknown }
  | { type: 'tool_result'; tool_use_id: string; content: string; is_error?: boolean }
  | { type: 'image'; source: { type: 'base64'; media_type: string; data: string } }

export interface Message {
  id: string
  sessionId: string
  role: 'user' | 'assistant' | 'system'
  content: string | ContentBlock[]  // Support both for backwards compat
  tokensUsed?: number
  createdAt: number
  metadata?: Record<string, unknown>
}

// Helper functions
export function getTextContent(message: Message): string {
  if (typeof message.content === 'string') return message.content
  return message.content
    .filter((b): b is { type: 'text'; text: string } => b.type === 'text')
    .map(b => b.text)
    .join('')
}

export function hasToolUse(message: Message): boolean {
  if (typeof message.content === 'string') return false
  return message.content.some(b => b.type === 'tool_use')
}
```

---

## Directory Ownership

This epic owns:
- `packages/core/src/permissions/` (new)
- `packages/core/src/tools/locks.ts` (new)
- `packages/platform-node/src/shell.ts` (modify kill())
- `packages/core/src/types/message.ts` (modify)

---

## Dependencies

- Epic 3 complete (ACP + Core)
- No dependencies on other infrastructure epics

---

## Acceptance Criteria

- [ ] Permission system prompts user before destructive operations
- [ ] SIGKILL escalation kills stubborn processes
- [ ] File locking prevents concurrent edit corruption
- [ ] Messages support mixed content blocks
- [ ] All existing tests pass
- [ ] ACP mode sends permission notifications
