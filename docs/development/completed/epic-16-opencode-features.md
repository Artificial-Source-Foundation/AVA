# Epic 16: OpenCode Feature Adoption

> Completed: 2026-02-03

## Goal

Adopt proven patterns from OpenCode to enhance tool output streaming, error handling, session management, and developer experience.

---

## Sprints Completed

| Sprint | Feature | Lines |
|--------|---------|-------|
| 16.1 | Metadata streaming + truncation enhancement | ~320 |
| 16.2 | Binary file detection + CorrectedError | ~200 |
| 16.3 | Session forking + instruction injection | ~570 |
| 16.4 | Typo suggestions + cleanup scheduler | ~570 |
| **Total** | | **~1,660** |

---

## Sprint 16.1: Metadata Streaming + Truncation

### What
- Added `ctx.metadata()` callback to ToolContext for progressive updates
- Created dedicated truncation module with metrics

### Files Created
- `packages/core/src/tools/truncation.ts` (~317 lines)

### Key Types
```typescript
interface TruncationResult {
  content: string
  truncated: boolean
  removedLines?: number
  removedBytes?: number
}

interface TruncationOptions {
  maxLines?: number
  maxBytes?: number
  direction?: 'head' | 'tail'
}
```

---

## Sprint 16.2: Binary Detection + CorrectedError

### What
- Enhanced binary file detection with null bytes and non-printable ratio checks
- Added CorrectedError class for permission rejections with user feedback

### Files Modified
- `packages/core/src/tools/utils.ts` - Binary detection (~150 lines added)
- `packages/core/src/permissions/types.ts` - CorrectedError class (~50 lines)

### Key Types
```typescript
interface BinaryCheckResult {
  isBinary: boolean
  reason: 'extension' | 'null_bytes' | 'non_printable_ratio' | 'none'
  confidence: 'high' | 'medium' | 'low'
}

class CorrectedError extends Error {
  permissionId: string
  correction?: string
  toolCallId?: string
}
```

---

## Sprint 16.3: Session Forking + Instruction Injection

### What
- Added `fork()` method to SessionManager for creating branched sessions
- Created instruction loading system for project/directory instructions

### Files Created
- `packages/core/src/instructions/types.ts` (~40 lines)
- `packages/core/src/instructions/loader.ts` (~249 lines)
- `packages/core/src/instructions/index.ts` (~10 lines)

### Files Modified
- `packages/core/src/session/types.ts` - ForkInfo, ForkOptions (~60 lines)
- `packages/core/src/session/manager.ts` - fork() method (~129 lines)

### Key Types
```typescript
interface ForkInfo {
  checkpointId: string
  messageId?: string
  originalTitle: string
  forkIndex: number
}

interface InstructionResult {
  instructions: InstructionFile[]
  sources: InstructionSource[]
}
```

### Instruction File Search Order
1. Directory-level: Walk up from file to project root
2. Project-level: `AGENTS.md`, `CLAUDE.md`, `instructions.md`
3. Global: `~/.ava/AGENTS.md`

---

## Sprint 16.4: Typo Suggestions + Cleanup Scheduler

### What
- Added fuzzy file suggestions when file not found
- Created background task scheduler for cleanup operations

### Files Created
- `packages/core/src/scheduler/types.ts` (~74 lines)
- `packages/core/src/scheduler/scheduler.ts` (~263 lines)
- `packages/core/src/scheduler/index.ts` (~10 lines)

### Files Modified
- `packages/core/src/tools/utils.ts` - findSimilarFiles() (~89 lines added)

### Key Types
```typescript
interface FileSuggestion {
  path: string
  similarity: number
  reason: 'similar_name' | 'same_extension' | 'common_typo'
}

interface ScheduledTask {
  id: string
  name: string
  intervalMs: number
  run: () => Promise<void>
  scope: TaskScope
}
```

---

## New Modules

### `instructions/` - Project Instruction Loading
| File | Lines | Purpose |
|------|-------|---------|
| types.ts | 40 | InstructionFile, InstructionSource, InstructionResult |
| loader.ts | 249 | InstructionLoader class, file discovery |
| index.ts | 10 | Exports |

### `scheduler/` - Background Task Scheduling
| File | Lines | Purpose |
|------|-------|---------|
| types.ts | 74 | ScheduledTask, TaskScope, TaskResult |
| scheduler.ts | 263 | Scheduler class, task management |
| index.ts | 10 | Exports |

---

## Key Patterns Adopted from OpenCode

1. **Metadata Streaming**: Progressive tool updates via callback
2. **Binary Detection**: Multi-strategy detection (extension, null bytes, ratio)
3. **CorrectedError**: Permission rejection with actionable feedback
4. **Session Forking**: Branch sessions from checkpoints
5. **Instruction Injection**: Project-level AI instructions
6. **Typo Suggestions**: Levenshtein-based file suggestions
7. **Background Scheduler**: Task scheduling for maintenance

---

## Backward Compatibility

All changes are additive:
- `metadata` callback is optional
- `parentId`, `fork` fields are optional
- `CorrectedError` extends Error (catchable)
- Instruction loading is opt-in
- Scheduler is passive unless used
