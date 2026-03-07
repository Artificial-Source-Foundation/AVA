# Epic 5: Context Management

> Token tracking, compaction, session state

---

## Goal

Enable long conversations without hitting context limits. Track tokens, implement compaction strategies, and persist full session state for resume.

---

## Reference Implementations

| Feature | Source | Stars |
|---------|--------|-------|
| Conversation compaction | Goose | 15k+ |
| Repo map / context | Aider | 25k+ |
| Session LRU cache | Goose | 15k+ |
| Token tracking | OpenCode | 70k+ |

---

## Sprints

| # | Sprint | Tasks | Est. Lines |
|---|--------|-------|------------|
| 5.1 | Token Tracking | Count tokens per message, track totals | ~200 |
| 5.2 | Compaction Strategies | Sliding window, summarization, hybrid | ~400 |
| 5.3 | Session State | Full state persistence, checkpoints | ~300 |
| 5.4 | Model Registry | Centralized model configs with context limits | ~200 |

**Total:** ~1100 lines

---

## Sprint 5.1: Token Tracking

### Files to Create

```
packages/core/src/context/
├── tracker.ts        # Token counting
├── index.ts          # Barrel export
```

### Implementation

```typescript
// tracker.ts
import { encode } from 'gpt-tokenizer'  // Or tiktoken

export interface TokenStats {
  messages: Map<string, number>  // messageId -> tokens
  total: number
  limit: number
  remaining: number
  percentUsed: number
}

export class ContextTracker {
  private stats: TokenStats

  constructor(private contextLimit: number) {
    this.stats = {
      messages: new Map(),
      total: 0,
      limit: contextLimit,
      remaining: contextLimit,
      percentUsed: 0,
    }
  }

  addMessage(id: string, content: string | ContentBlock[]): number {
    const text = typeof content === 'string'
      ? content
      : JSON.stringify(content)
    const tokens = encode(text).length

    this.stats.messages.set(id, tokens)
    this.stats.total += tokens
    this.stats.remaining = this.stats.limit - this.stats.total
    this.stats.percentUsed = (this.stats.total / this.stats.limit) * 100

    return tokens
  }

  removeMessage(id: string): void {
    const tokens = this.stats.messages.get(id) ?? 0
    this.stats.messages.delete(id)
    this.stats.total -= tokens
    this.stats.remaining = this.stats.limit - this.stats.total
    this.stats.percentUsed = (this.stats.total / this.stats.limit) * 100
  }

  shouldCompact(threshold = 80): boolean {
    return this.stats.percentUsed >= threshold
  }

  getStats(): TokenStats {
    return { ...this.stats }
  }
}
```

---

## Sprint 5.2: Compaction Strategies

### Files to Create

```
packages/core/src/context/
├── compactor.ts              # Main compaction logic
├── strategies/
│   ├── sliding-window.ts     # Keep last N messages
│   ├── summarize.ts          # LLM summarization
│   └── hierarchical.ts       # Tree structure (Goose)
└── index.ts
```

### Compactor Interface

```typescript
export interface CompactionStrategy {
  name: string
  compact(messages: Message[], targetTokens: number): Promise<Message[]>
}

export class Compactor {
  constructor(
    private strategies: CompactionStrategy[],
    private tracker: ContextTracker
  ) {}

  async compact(messages: Message[], targetPercent = 50): Promise<Message[]> {
    const targetTokens = Math.floor(this.tracker.getStats().limit * (targetPercent / 100))

    // Try strategies in order until one succeeds
    for (const strategy of this.strategies) {
      try {
        const compacted = await strategy.compact(messages, targetTokens)
        return compacted
      } catch (err) {
        console.warn(`Strategy ${strategy.name} failed:`, err)
      }
    }

    // Fallback: just keep last N messages
    return messages.slice(-10)
  }
}
```

### Sliding Window Strategy

```typescript
export const slidingWindow: CompactionStrategy = {
  name: 'sliding-window',
  async compact(messages, targetTokens) {
    // Always keep system message
    const system = messages.find(m => m.role === 'system')
    const others = messages.filter(m => m.role !== 'system')

    // Keep messages from end until we hit target
    const kept: Message[] = []
    let tokens = system ? encode(getTextContent(system)).length : 0

    for (let i = others.length - 1; i >= 0; i--) {
      const msgTokens = encode(getTextContent(others[i])).length
      if (tokens + msgTokens > targetTokens) break
      kept.unshift(others[i])
      tokens += msgTokens
    }

    return system ? [system, ...kept] : kept
  }
}
```

### Summarization Strategy

```typescript
export const summarize: CompactionStrategy = {
  name: 'summarize',
  async compact(messages, targetTokens) {
    // Keep system and last few messages
    const system = messages.find(m => m.role === 'system')
    const recent = messages.slice(-6)  // Last 3 turns
    const toSummarize = messages.slice(0, -6).filter(m => m.role !== 'system')

    if (toSummarize.length === 0) {
      return system ? [system, ...recent] : recent
    }

    // Summarize older messages via LLM
    const summary = await summarizeMessages(toSummarize)

    const summaryMessage: Message = {
      id: 'summary-' + Date.now(),
      sessionId: messages[0].sessionId,
      role: 'system',
      content: `Previous conversation summary:\n${summary}`,
      createdAt: Date.now(),
    }

    return system
      ? [system, summaryMessage, ...recent]
      : [summaryMessage, ...recent]
  }
}
```

---

## Sprint 5.3: Session State

### Files to Create

```
packages/core/src/session/
├── types.ts          # SessionState, Checkpoint
├── manager.ts        # Save/restore, checkpoints
└── index.ts
```

### Session State

```typescript
export interface SessionState {
  id: string
  messages: Message[]
  workingDirectory: string
  toolCallCount: number
  tokenStats: TokenStats
  openFiles: Map<string, FileState>
  env: Record<string, string>
  checkpoint?: Checkpoint
  createdAt: number
  updatedAt: number
}

export interface FileState {
  path: string
  content: string
  mtime: number
  dirty: boolean
}

export interface Checkpoint {
  id: string
  timestamp: number
  gitSha?: string
  description: string
  messageCount: number
}

export class SessionManager {
  private sessions = new Map<string, SessionState>()
  private maxSessions = 10  // LRU limit

  async save(state: SessionState): Promise<void> {
    // Persist to database/file
  }

  async restore(sessionId: string): Promise<SessionState | null> {
    // Load from database/file
  }

  async createCheckpoint(sessionId: string, description: string): Promise<Checkpoint> {
    // Snapshot current state
  }

  async rollbackToCheckpoint(sessionId: string, checkpointId: string): Promise<void> {
    // Restore from checkpoint
  }
}
```

---

## Sprint 5.4: Model Registry

### Files to Create

```
packages/core/src/models/
├── types.ts          # ModelConfig, ModelCapabilities
├── registry.ts       # Model definitions
└── index.ts
```

### Model Registry

```typescript
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
    json: boolean
  }
  pricing?: {
    inputPer1k: number
    outputPer1k: number
  }
}

export const MODEL_REGISTRY: Record<string, ModelConfig> = {
  'claude-sonnet-4': {
    id: 'claude-sonnet-4-20250514',
    provider: 'anthropic',
    displayName: 'Claude Sonnet 4',
    contextWindow: 200000,
    maxOutputTokens: 16384,
    capabilities: { tools: true, vision: true, streaming: true, json: true },
    pricing: { inputPer1k: 0.003, outputPer1k: 0.015 },
  },
  'claude-opus-4': {
    id: 'claude-opus-4-20250514',
    provider: 'anthropic',
    displayName: 'Claude Opus 4',
    contextWindow: 200000,
    maxOutputTokens: 16384,
    capabilities: { tools: true, vision: true, streaming: true, json: true },
    pricing: { inputPer1k: 0.015, outputPer1k: 0.075 },
  },
  'gpt-4o': {
    id: 'gpt-4o',
    provider: 'openai',
    displayName: 'GPT-4o',
    contextWindow: 128000,
    maxOutputTokens: 16384,
    capabilities: { tools: true, vision: true, streaming: true, json: true },
    pricing: { inputPer1k: 0.005, outputPer1k: 0.015 },
  },
  // ... more models
}

export function getModel(id: string): ModelConfig | undefined {
  return MODEL_REGISTRY[id]
}

export function getContextLimit(id: string): number {
  return MODEL_REGISTRY[id]?.contextWindow ?? 128000
}
```

---

## Directory Ownership

This epic owns:
- `packages/core/src/context/` (new)
- `packages/core/src/session/` (new)
- `packages/core/src/models/` (new)

---

## Dependencies

- Epic 3 complete (ACP + Core)
- Epic 4.4 (Multi-part Messages) for proper token counting

---

## Acceptance Criteria

- [ ] Token tracking shows usage per message and total
- [ ] Auto-compaction triggers at 80% context usage
- [ ] Session state persists across restarts
- [ ] Checkpoints allow rollback to previous state
- [ ] Model registry provides context limits for all supported models
