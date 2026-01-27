# AGENTS.md

> Universal agent instructions for Delta9 - OpenCode Plugin for Strategic AI Coordination

---

## Quick Start

```bash
npm run build      # Build TypeScript
npm run test       # Run 1165+ Vitest tests
npm run lint       # ESLint check
npm run typecheck  # TypeScript strict check
```

**Key Files**:
- `CLAUDE.md` - Project overview with code examples
- `docs/spec.md` - Full specification (source of truth)
- `src/lib/index.ts` - All 183 library exports

---

## Project Architecture

```
src/
├── index.ts              # Plugin entry point
├── agents/               # 19 agent definitions
│   ├── commander.ts      # Lead planner (NEVER writes code)
│   ├── operator.ts       # Task executor
│   ├── validator.ts      # QA verification gate
│   ├── loader.ts         # Dynamic agent loading (NEW)
│   ├── council/          # 4 Oracle agents (CIPHER, VECTOR, PRISM, APEX)
│   └── support/          # 8 Delta Team agents
├── lib/                  # 183 exports - utilities & patterns
│   ├── concurrency-manager.ts  # Per-provider API limits
│   ├── process-cleanup.ts      # Graceful shutdown
│   ├── event-store.ts          # Event sourcing
│   ├── session-isolation.ts    # Multi-user isolation
│   └── ...                     # 20+ more modules
├── mission/              # Mission state management
├── tools/                # 70+ custom tools
├── hooks/                # OpenCode event hooks
└── types/                # TypeScript definitions
```

---

## Code Standards

### TypeScript
- **Strict mode** required
- **No `any` type** - use `unknown` with type guards
- **Zod** for all runtime validation
- **kebab-case** filenames, **camelCase** functions, **PascalCase** types

### File Limits
- Max **300 lines** per file
- One component per file
- **80%+ test coverage** target

---

## Agent Hierarchy

### Command Layer
- **Commander** - Strategic planning only. NEVER writes code.

### Council Layer (4 Oracles)
- **CIPHER** (Claude) - Architecture & edge cases
- **VECTOR** (GPT) - Code patterns & logic
- **PRISM** (Gemini) - UI/UX & creativity
- **APEX** (DeepSeek) - Performance & algorithms

### Execution Layer
- **Operator** - Task executor (focused, minimal changes)
- **Validator** - QA gate (returns PASS/FIXABLE/FAIL)

### Delta Team (8 Support Agents)
- **RECON** - Codebase reconnaissance
- **SIGINT** - Intelligence research
- **TACCOM** - Tactical command advisor
- **SURGEON** - Surgical precision fixer
- **SENTINEL** - Quality assurance guardian
- **SCRIBE** - Documentation writer
- **FACADE** - Frontend operations
- **SPECTRE** - Visual intelligence

---

## Production Patterns (14 Modules)

### Critical Infrastructure

| Pattern | Module | Usage |
|---------|--------|-------|
| Concurrency | `concurrency-manager.ts` | `withConcurrencySlot(provider, model, session, fn)` |
| Cleanup | `process-cleanup.ts` | `registerCleanup({ name, priority, handler })` |
| Events | `event-store.ts` | `getEventStore().append(event)` |
| Sessions | `session-isolation.ts` | `registerSession(child, parent)` |

### High Value Patterns

| Pattern | Module | Usage |
|---------|--------|-------|
| Deduplication | `injection-tracker.ts` | `tryInject(session, contextType)` |
| Storage | `storage-adapter.ts` | `FileStorageAdapter`, `MemoryStorageAdapter` |
| Search | `semantic-search.ts` | `semanticSearch(items, query, config)` |
| Multi-Mode | `multi-mode-tool.ts` | `createMultiModeDispatcher(config)` |
| Maintenance | `idle-maintenance.ts` | `getIdleMaintenanceManager().triggerIdle()` |
| Compliance | `compliance-hooks.ts` | `checkAndTrack(session, tool, role)` |
| Notifications | `notifications.ts` | `showToast()`, `BatchNotificationManager` |
| Agent Loading | `agents/loader.ts` | `createAgentLoader().loadFromDirectory()` |

---

## Boundaries

### Always Do
- Read `docs/spec.md` before architectural changes
- Validate input with Zod schemas
- Use concurrency manager for API calls
- Register cleanup handlers for graceful shutdown
- Test before committing

### Never Do
- Use `any` type
- Mutate mission state directly (use MissionState class)
- Skip the Validator gate
- Let Commander write code
- Bypass rate limiter or concurrency limits
- Hardcode model names (use config)

---

## Mission State

State persists in `.delta9/mission.json`. Never modify directly.

```typescript
const state = new MissionState()
state.load()                    // Load from disk
state.create('description')     // Create new mission
state.addObjective({...})       // Add objective
state.updateTask(id, {...})     // Update task
state.save()                    // Persist to disk
```

---

## Testing

- **Framework**: Vitest
- **Location**: `tests/` directory
- **Run**: `npm test` or `npm test -- --run`
- **Coverage**: `npm test -- --coverage`

---

## Key Documentation

| Priority | Path | Description |
|----------|------|-------------|
| 1 | `CLAUDE.md` | Project overview, code examples |
| 2 | `docs/spec.md` | Full specification (source of truth) |
| 3 | `docs/delta9/agents.md` | All 19 agents detailed |
| 4 | `docs/plugin-guide/` | Plugin development (14 files) |
| 5 | `docs/patterns/` | Best practices |

---

## Architecture Flow

```
User Request → Commander (plan) → Council (deliberate) → Mission Plan
                                                              ↓
User Approval ← ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─┘
      ↓
Commander (dispatch) → Operator (execute) → Validator (verify)
                                                   ↓
                                            PASS / FIXABLE / FAIL
```

---

## Stats

- **Version**: 0.1.0
- **Tests**: 1165+ passing
- **Exports**: 183 from lib
- **Tools**: 70+
- **Agents**: 19
- **Patterns**: 14 production patterns
