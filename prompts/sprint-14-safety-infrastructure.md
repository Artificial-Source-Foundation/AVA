# Sprint 14: Safety & Infrastructure — Implementation Prompt

> For AI coding agent. Estimated: 6 features, mix M/L effort.
> Run `npm run test:run && npx tsc --noEmit` after each feature.

---

## Role

You are implementing Sprint 14 (Safety & Infrastructure) for AVA, a multi-agent AI coding assistant.

Read these files first:
- `CLAUDE.md` (conventions, architecture, dispatchCompute pattern)
- `AGENTS.md` (code standards, common workflows)

---

## Pre-Implementation: Competitor Research Phase

**CRITICAL**: Before implementing each feature, you MUST read the relevant competitor reference code and extract best patterns. The reference code is in `docs/reference-code/`. The audit summaries are in `docs/research/audits/`.

For EACH feature below:
1. **Read** the listed competitor reference files
2. **Extract** the key algorithm, data structures, thresholds, and edge cases
3. **Adapt** to AVA's TypeScript + dispatchCompute architecture
4. **Implement** using AVA conventions (strict TS, no `any`, explicit return types, <300 lines/file)
5. **Test** with unit tests
6. **Verify** by running `npm run test:run && npx tsc --noEmit`

---

## Feature 1: Diff Sandbox / Review Pipeline

### Competitor Research
Read these files:
- `docs/reference-code/plandex/app/server/model/plan/build.go` — How changes are stored before applying to filesystem
- `docs/research/audits/plandex-audit.md` — "Worth Stealing" section on diff sandbox

### What to Build
All agent changes are staged in memory before touching the filesystem. User reviews and approves before applying.

**File:** `packages/extensions/diff/src/sandbox.ts` (new)

```typescript
export interface SandboxedChange {
  id: string
  file: string
  type: 'create' | 'modify' | 'delete'
  originalContent: string | null
  newContent: string
  diff: string  // unified diff
  timestamp: number
}

export class DiffSandbox {
  private pending: Map<string, SandboxedChange> = new Map()

  /** Stage a change without applying to filesystem */
  stage(change: Omit<SandboxedChange, 'id' | 'timestamp' | 'diff'>): SandboxedChange

  /** Get all pending changes */
  getPending(): SandboxedChange[]

  /** Apply a specific change to filesystem */
  apply(id: string): Promise<void>

  /** Apply all pending changes */
  applyAll(): Promise<void>

  /** Reject a specific change */
  reject(id: string): void

  /** Clear all pending changes */
  clear(): void
}
```

**Implementation:**
- Middleware (priority 2, before sandbox) intercepts write/edit/create/delete tools
- Instead of writing to disk, stages the change in `DiffSandbox`
- Emits `diff:staged` event with the change for UI display
- UI shows pending changes panel with approve/reject per file
- Config: `diff.sandbox.enabled` (default: false — opt-in)
- When disabled, tools write directly as before

**Integration:** Register in `packages/extensions/diff/src/index.ts`. Wire to existing undo/redo system.

### Tests
- `packages/extensions/diff/src/sandbox.test.ts`
- Test: stage → getPending returns change
- Test: apply writes to filesystem
- Test: reject removes from pending
- Test: applyAll writes all and clears
- Test: disabled config bypasses sandbox

---

## Feature 2: Conseca Dynamic Policies

### Competitor Research
Read these files:
- `docs/reference-code/gemini-cli/packages/core/src/safety/conseca/` — All files in this directory
- `docs/research/audits/gemini-cli-audit.md` — "Worth Stealing" section on Conseca

### What to Build
LLM generates least-privilege security policies based on the user's task. Policies restrict which tools/paths/commands are allowed.

**File:** `packages/extensions/permissions/src/conseca.ts` (new)

```typescript
export interface SecurityPolicy {
  allowedTools: string[]
  allowedPaths: string[]  // glob patterns
  deniedCommands: string[]  // shell command patterns
  networkAccess: boolean
  reasoning: string
}

/** Generate a least-privilege policy from the user's goal */
export async function generatePolicy(
  goal: string,
  availableTools: string[],
  workingDirectory: string,
  provider: LLMProvider,
  model: string
): Promise<SecurityPolicy>

/** Enforce policy on a tool call */
export function enforcePolicy(
  policy: SecurityPolicy,
  toolName: string,
  args: Record<string, unknown>
): { allowed: boolean; reason?: string }
```

**Implementation:**
- Phase 1: At session start, call LLM with user's goal → generates `SecurityPolicy`
- Phase 2: Middleware (priority 1, highest) checks each tool call against policy
- Policy prompt: "Given the goal '{goal}', what's the minimum set of tools and paths needed? Respond with JSON."
- Use a cheap/fast model for policy generation (not the main agent model)
- Config: `permissions.conseca.enabled` (default: false — opt-in)
- Cache policy per session (don't regenerate every turn)

**Integration:** Register as middleware in `packages/extensions/permissions/src/index.ts`.

### Tests
- `packages/extensions/permissions/src/conseca.test.ts`
- Test: policy restricts tool not in allowedTools
- Test: policy allows tool in allowedTools
- Test: path glob matching works
- Test: denied command patterns block shell execution
- Test: policy is cached per session

---

## Feature 3: Event-Sourced Architecture

### Competitor Research
Read these files:
- `docs/reference-code/openhands/openhands/events/` — All files in events directory
- `docs/research/audits/openhands-audit.md` — "Worth Stealing" section on event sourcing

### What to Build
Record all agent events as an immutable log. Enables replay, time-travel debugging, and deterministic re-execution.

**File:** `packages/core-v2/src/events/event-store.ts` (new)

```typescript
export interface StoredEvent {
  id: string
  sessionId: string
  timestamp: number
  type: string
  payload: Record<string, unknown>
  parentEventId?: string  // for causality tracking
}

export class EventStore {
  /** Append event to the log */
  append(event: Omit<StoredEvent, 'id' | 'timestamp'>): StoredEvent

  /** Get all events for a session */
  getSession(sessionId: string): StoredEvent[]

  /** Get events by type */
  getByType(sessionId: string, type: string): StoredEvent[]

  /** Get events in time range */
  getRange(sessionId: string, start: number, end: number): StoredEvent[]

  /** Export session as replayable JSON */
  export(sessionId: string): string
}
```

**Implementation:**
- Subscribe to ALL agent events via the message bus
- Append each event to an append-only log (SQLite table: `event_log`)
- Schema: `id TEXT PRIMARY KEY, session_id TEXT, timestamp INTEGER, type TEXT, payload TEXT, parent_event_id TEXT`
- Index on `(session_id, timestamp)` for efficient session queries
- Export produces JSON array of events for replay
- Replay is read-only for now (future: deterministic re-execution)
- Use `dispatchCompute` for SQLite operations with TS fallback using `better-sqlite3`

**Integration:** Register as a listener in a new file `packages/extensions/context/src/event-store.ts` that activates with the context extension.

### Tests
- `packages/core-v2/src/events/event-store.test.ts`
- Test: append and retrieve events
- Test: session isolation (events from different sessions don't mix)
- Test: time range queries work
- Test: export produces valid JSON
- Test: high-volume append performance (1000 events < 1s)

---

## Feature 4: Dynamic Provider Loading

### Competitor Research
Read these files:
- `docs/reference-code/opencode/packages/opencode/src/provider/` — Provider loading system
- `docs/research/audits/opencode-audit.md` — "Worth Stealing" section on providers

### What to Build
Load LLM providers dynamically — bundled SDKs + external registry + runtime npm install.

**File:** `packages/extensions/providers/src/dynamic-loader.ts` (new)

```typescript
export interface ProviderManifest {
  name: string
  package: string      // npm package name
  version?: string
  factory: string      // export name for factory function
  models: string[]     // known model IDs
  authEnvVar?: string  // e.g., 'OPENAI_API_KEY'
}

/** Load a provider by name, installing if needed */
export async function loadProvider(
  name: string,
  registry?: ProviderManifest[]
): Promise<LLMClientFactory>

/** Fetch available providers from external registry */
export async function fetchRegistry(
  url?: string
): Promise<ProviderManifest[]>
```

**Implementation:**
- Bundled providers: anthropic, openai, openrouter, google (already exist)
- External registry: fetch from configurable URL (default: none — user provides)
- Runtime install: `npm install <package>@<version>` in `~/.ava/providers/`
- Dynamic import: `import(resolvedPath)` → extract factory
- Cache installed providers in `~/.ava/providers/manifest.json`
- Config: `providers.registry.url` and `providers.autoInstall` (default: false)

**Integration:** Extend `packages/extensions/providers/src/index.ts` to use dynamic loader as fallback when bundled provider not found.

### Tests
- `packages/extensions/providers/src/dynamic-loader.test.ts`
- Test: bundled provider loads without install
- Test: unknown provider triggers install flow (mock npm)
- Test: manifest caching works
- Test: registry fetch returns provider list
- Test: autoInstall=false blocks runtime install

---

## Feature 5: Shadow Git Snapshots

### Competitor Research
Read these files:
- `docs/reference-code/opencode/packages/opencode/src/session/snapshot/` — Snapshot system
- `docs/research/audits/opencode-audit.md` — "Worth Stealing" section on snapshots

### What to Build
Isolated git repos for snapshots that don't pollute project history.

**File:** `packages/extensions/git/src/shadow-snapshots.ts` (new)

```typescript
export interface Snapshot {
  id: string
  sessionId: string
  timestamp: number
  message: string
  commitHash: string
}

export class ShadowSnapshotManager {
  constructor(private projectDir: string, private snapshotDir?: string)

  /** Initialize shadow repo at ~/.ava/snapshots/<project-hash>/ */
  init(): Promise<void>

  /** Take a snapshot of current project state */
  take(sessionId: string, message: string): Promise<Snapshot>

  /** List snapshots for a session */
  list(sessionId: string): Promise<Snapshot[]>

  /** Restore project to a snapshot */
  restore(snapshotId: string): Promise<void>

  /** Prune old snapshots (keep last N per session) */
  prune(keepPerSession?: number): Promise<number>
}
```

**Implementation:**
- Shadow repo lives at `~/.ava/snapshots/<project-hash>/` (not in project dir)
- Uses git internally but project never sees it
- `take()`: copy changed files to shadow repo, commit with metadata
- `restore()`: checkout snapshot commit, copy files back to project
- Auto-snapshot before destructive operations (delete, overwrite)
- Scheduled pruning: keep last 10 snapshots per session, run on session end
- Use `dispatchCompute` for git operations with shell fallback

**Integration:** Register in `packages/extensions/git/src/index.ts`. Hook into `tool:before` for destructive operations.

### Tests
- `packages/extensions/git/src/shadow-snapshots.test.ts`
- Test: init creates shadow repo
- Test: take creates snapshot with correct metadata
- Test: restore recovers file state
- Test: prune removes old snapshots
- Test: shadow repo doesn't affect project git status

---

## Feature 6: 3-Layer Inspection Pipeline

### Competitor Research
Read these files:
- `docs/reference-code/goose/crates/goose/src/agents/tool_inspection.rs` — Inspection pipeline
- `docs/research/audits/goose-audit.md` — "Worth Stealing" section on inspection

### What to Build
Structured pipeline: Security → Permission → Repetition inspectors with typed results.

**File:** `packages/extensions/permissions/src/inspection-pipeline.ts` (new)

```typescript
export type InspectionResult =
  | { action: 'allow' }
  | { action: 'deny'; reason: string }
  | { action: 'escalate'; reason: string }  // ask user

export interface Inspector {
  name: string
  layer: 'security' | 'permission' | 'repetition'
  inspect(toolName: string, args: Record<string, unknown>, context: ToolContext): Promise<InspectionResult>
}

export class InspectionPipeline {
  private inspectors: Inspector[] = []

  register(inspector: Inspector): void

  /** Run all inspectors in layer order. First deny/escalate wins. */
  async inspect(toolName: string, args: Record<string, unknown>, context: ToolContext): Promise<InspectionResult>
}
```

**Built-in inspectors:**
1. **SecurityInspector** (layer: security) — Check for dangerous patterns: `rm -rf /`, `chmod 777`, env variable injection, path traversal
2. **PermissionInspector** (layer: permission) — Check tool permissions against user's allowlist/denylist config
3. **RepetitionInspector** (layer: repetition) — Detect repeated identical calls (integrates with existing doom-loop detection)

**Implementation:**
- Inspectors run in layer order: security → permission → repetition
- First `deny` or `escalate` result short-circuits the pipeline
- `escalate` pauses execution and asks user for approval
- Results are typed and merged: multiple `allow` = allow, any `deny` = deny
- Register as middleware (priority 1) in permissions extension
- Replaces current ad-hoc permission checks with structured pipeline

**Integration:** Register in `packages/extensions/permissions/src/index.ts`.

### Tests
- `packages/extensions/permissions/src/inspection-pipeline.test.ts`
- Test: all allow → pipeline allows
- Test: security deny → short-circuits, permission not called
- Test: escalate pauses and returns reason
- Test: layer ordering is correct (security first)
- Test: dangerous command patterns caught by security inspector

---

## Post-Implementation Verification

After ALL 6 features:

1. `npm run test:run`
2. `npx tsc --noEmit`
3. `npm run lint`
4. `npm run format:check`
5. Verify no files exceed 300 lines
6. Commit: `git commit -m "feat(sprint-14): safety & infrastructure — diff sandbox, conseca policies, event sourcing, dynamic providers, shadow snapshots, inspection pipeline"`

---

## File Change Summary

| Action | File |
|--------|------|
| CREATE | `packages/extensions/diff/src/sandbox.ts` |
| CREATE | `packages/extensions/diff/src/sandbox.test.ts` |
| MODIFY | `packages/extensions/diff/src/index.ts` (register sandbox) |
| CREATE | `packages/extensions/permissions/src/conseca.ts` |
| CREATE | `packages/extensions/permissions/src/conseca.test.ts` |
| MODIFY | `packages/extensions/permissions/src/index.ts` (register conseca + pipeline) |
| CREATE | `packages/core-v2/src/events/event-store.ts` |
| CREATE | `packages/core-v2/src/events/event-store.test.ts` |
| CREATE | `packages/extensions/context/src/event-store.ts` (listener registration) |
| CREATE | `packages/extensions/providers/src/dynamic-loader.ts` |
| CREATE | `packages/extensions/providers/src/dynamic-loader.test.ts` |
| MODIFY | `packages/extensions/providers/src/index.ts` (fallback to dynamic loader) |
| CREATE | `packages/extensions/git/src/shadow-snapshots.ts` |
| CREATE | `packages/extensions/git/src/shadow-snapshots.test.ts` |
| MODIFY | `packages/extensions/git/src/index.ts` (register snapshot hooks) |
| CREATE | `packages/extensions/permissions/src/inspection-pipeline.ts` |
| CREATE | `packages/extensions/permissions/src/inspection-pipeline.test.ts` |
