# Delta9 - OpenCode Plugin

> Strategic AI Coordination for Mission-Critical Development

Delta9 is an OpenCode plugin implementing a hierarchical **Commander + Council + Operators** architecture that separates planning from execution, maintains mission state across context compactions, and verifies all work against acceptance criteria.

---

## Commands

```bash
npm run build      # Build TypeScript
npm run test       # Run Vitest tests
npm run lint       # ESLint check
npm run typecheck  # TypeScript strict check
```

---

## Documentation (Start Here)

| Priority | Path | Description |
|----------|------|-------------|
| **1** | `docs/spec.md` | Full specification (SOURCE OF TRUTH) |
| **2** | `docs/README.md` | Documentation navigation hub |
| **3** | `docs/plugin-guide/` | How to build OpenCode plugins (14 files) |
| **4** | `docs/opencode/` | OpenCode platform reference |
| **5** | `docs/delta9/` | Delta9 architecture & API |
| **6** | `docs/patterns/` | Best practices |

---

## Project Structure

```
delta9/
├── src/
│   ├── index.ts              # Plugin entry point
│   ├── agents/               # Agent definitions (19 agents)
│   │   ├── commander.ts      # Lead planner & orchestrator
│   │   ├── operator.ts       # Task executor
│   │   ├── validator.ts      # QA verification
│   │   ├── loader.ts         # Dynamic agent loading (NEW)
│   │   ├── council/          # 4 Oracle agents
│   │   └── support/          # 8 Delta Team agents
│   ├── mission/              # Mission state management
│   ├── orchestration/        # Council orchestration
│   ├── routing/              # Task routing
│   ├── hooks/                # OpenCode event hooks
│   ├── tools/                # Custom tools (70+ tools)
│   ├── lib/                  # Utilities (183 exports)
│   └── types/                # TypeScript types
├── docs/                     # Documentation
├── tests/                    # 1165+ unit tests
└── package.json
```

---

## Key Concepts

| Concept | Description |
|---------|-------------|
| **Commander** | Lead planner. NEVER writes code. |
| **Council** | 4 Oracles: CIPHER, VECTOR, PRISM, APEX |
| **Delta Team** | 8 support agents: RECON, SIGINT, TACCOM, SURGEON, SENTINEL, SCRIBE, FACADE, SPECTRE |
| **Operators** | Task executors |
| **Validator** | QA gate |
| **Mission State** | Persisted in `.delta9/mission.json` |

---

## Library Modules (src/lib/)

### Core Infrastructure

| Module | Purpose | Key Exports |
|--------|---------|-------------|
| `paths.ts` | File path utilities | `getDelta9Dir`, `getMissionPath` |
| `config.ts` | Configuration loading | `loadConfig`, `getConfig` |
| `logger.ts` | Structured logging | `getNamedLogger`, `debug`, `info` |
| `errors.ts` | Error handling | `Delta9Error`, `errors` |
| `hints.ts` | Contextual hints | `getHint`, `hints` |

### Agent Infrastructure

| Module | Purpose | Key Exports |
|--------|---------|-------------|
| `background-manager.ts` | Background task management | `BackgroundManager`, `getBackgroundManager` |
| `models.ts` | Model resolution | `getModelForRole`, `getFallbackChain` |
| `budget.ts` | Cost tracking | `BudgetManager`, `MODEL_COSTS` |
| `rate-limiter.ts` | Rate limit handling | `RateLimiter`, `getBestFallback` |

### Production Patterns (NEW - 14 Patterns)

| Module | Purpose | Key Exports |
|--------|---------|-------------|
| `concurrency-manager.ts` | Per-provider concurrency limits | `ProviderConcurrencyManager`, `withConcurrencySlot` |
| `process-cleanup.ts` | Graceful shutdown handlers | `ProcessCleanupManager`, `registerCleanup` |
| `event-store.ts` | Event sourcing with snapshots | `EventStore`, `getEventStore` |
| `session-isolation.ts` | Root session tracking | `SessionIsolationManager`, `registerSession` |
| `injection-tracker.ts` | Context deduplication | `InjectionTracker`, `tryInject` |
| `storage-adapter.ts` | Storage abstraction | `FileStorageAdapter`, `MemoryStorageAdapter` |
| `semantic-search.ts` | Weighted memory search | `semanticSearch`, `searchMemoryBlocks` |
| `multi-mode-tool.ts` | Multi-mode tool dispatcher | `MultiModeDispatcher`, `createMultiModeDispatcher` |
| `idle-maintenance.ts` | Background maintenance | `IdleMaintenanceManager`, `triggerIdle` |
| `compliance-hooks.ts` | Workflow compliance | `checkCompliance`, `registerDefaultRules` |
| `notifications.ts` | Toast + batch notifications | `showToast`, `BatchNotificationManager` |

### Dynamic Agent Loading (NEW)

| Module | Purpose | Key Exports |
|--------|---------|-------------|
| `agents/loader.ts` | Load agents from markdown | `AgentLoader`, `createAgentLoader` |

---

## Production Patterns Reference

### 1. Concurrency Manager
Per-provider slot limits with queuing (Anthropic:4, OpenAI:5, Google:5, DeepSeek:3).

```typescript
import { withConcurrencySlot } from './lib/concurrency-manager.js'
await withConcurrencySlot('anthropic', 'opus', sessionId, async () => {
  // Your API call here
})
```

### 2. Process Cleanup
Graceful shutdown with priority-ordered handlers.

```typescript
import { registerCleanup, CleanupPriority } from './lib/process-cleanup.js'
registerCleanup({
  name: 'save-state',
  priority: CleanupPriority.CRITICAL,
  handler: async () => saveMissionState()
})
```

### 3. Event Store
Event sourcing with snapshots for state reconstruction.

```typescript
import { getEventStore } from './lib/event-store.js'
const store = getEventStore()
await store.append({ type: 'task_completed', taskId, timestamp: Date.now() })
```

### 4. Session Isolation
Root session tracking for multi-user background task isolation.

```typescript
import { registerSession, getRootSession } from './lib/session-isolation.js'
registerSession(childSessionId, parentSessionId)
const root = getRootSession(anySessionId)
```

### 5. Compliance Hooks
Workflow enforcement (Commander delegates, Operators validate).

```typescript
import { checkAndTrack, registerDefaultRules } from './lib/compliance-hooks.js'
registerDefaultRules()
const result = checkAndTrack(sessionId, toolName, 'commander')
if (result.hasViolation) console.log(result.reminder)
```

### 6. Dynamic Agent Loading
Load custom agents from `.delta9/agents/` with YAML frontmatter.

```markdown
---
name: custom-reviewer
role: reviewer
model: claude-sonnet-4-20250514
tools: [read_file, grep]
constraints:
  - Never modify files
---

You are a code reviewer agent...
```

---

## Quick Pattern Lookup

| Need | Look At |
|------|---------|
| Plugin entry | `docs/plugin-guide/01-architecture.md` |
| Configuration | `docs/plugin-guide/02-configuration.md` |
| Hook system | `docs/plugin-guide/03-hooks.md` |
| Tool definition | `docs/plugin-guide/04-tools.md` |
| Memory | `docs/plugin-guide/05-memory.md` |
| Background tasks | `docs/plugin-guide/08-background-tasks.md` |
| Patterns | `docs/plugin-guide/11-patterns.md` |

---

## Coding Conventions

- **TypeScript**: Strict mode, no `any`
- **Validation**: Zod for all runtime data
- **Naming**: camelCase functions, PascalCase types, kebab-case files
- **Files**: Max 300 lines, one component per file
- **Testing**: Vitest with 80%+ coverage target

---

## Boundaries

### Always Do

- Read `docs/spec.md` before architectural changes
- Validate runtime data with Zod
- Persist state via `MissionState` class
- Test before committing
- Use concurrency manager for API calls
- Register cleanup handlers for graceful shutdown

### Never Do

- Use `any` type
- Mutate mission state directly
- Skip Validator gate
- Let Commander write code
- Bypass rate limiter or concurrency limits

---

## Current Status

- **Version**: 0.1.0
- **Phase**: Production Ready
- **Tests**: 1165+ unit tests passing
- **Exports**: 183 from src/lib/index.ts
- **Tools**: 70+ tools across 19 categories
- **Agents**: 19 agents (Commander, 4 Oracles, 8 Support, Operators, Validator)

### Key Capabilities

- Mission state persistence & recovery
- Council orchestration (4 modes)
- Event sourcing (48 event types)
- Learning system with anti-pattern detection
- Agent messaging with auto-resume
- Task decomposition (6 strategies)
- Decision traces with precedent chains
- Async subagent system
- Real-time TUI dashboard
- **14 production patterns** from reference plugins

See `docs/BACKLOG.md` for detailed task tracking.
