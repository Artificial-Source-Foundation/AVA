# Epic 6: Developer Experience

> Tool.define(), diffs, git snapshots

---

## Goal

Improve the developer experience for both AVA contributors and users. Better tool definitions with validation, unified diff tracking, and git-based safety snapshots.

---

## Reference Implementations

| Feature | Source | Stars |
|---------|--------|-------|
| Tool.define() with Zod | OpenCode | 70k+ |
| Unified diffs | Aider | 25k+ |
| Git auto-commits | Aider | 25k+ |
| Plan/branch model | Plandex | 8k+ |

---

## Sprints

| # | Sprint | Tasks | Est. Lines |
|---|--------|-------|------------|
| 6.1 | Tool.define() | Zod validation, metadata, cleaner API | ~250 |
| 6.2 | Diff Tracking | Unified diffs, pending edits | ~250 |
| 6.3 | Git Snapshots | Auto-commits, rollback | ~250 |

**Total:** ~750 lines

---

## Sprint 6.1: Tool.define()

### Files to Create

```
packages/core/src/tools/
├── define.ts         # Tool.define() wrapper
├── validation.ts     # Zod schema utilities
```

### Dependencies

```bash
pnpm add zod zod-to-json-schema -w --filter @ava/core
```

### Implementation

```typescript
// define.ts
import { z } from 'zod'
import { zodToJsonSchema } from 'zod-to-json-schema'
import type { Tool, ToolContext, ToolResult, ToolDefinition } from './types.js'

export interface ToolConfig<T extends z.ZodType> {
  name: string
  description: string
  schema: T
  execute: (input: z.infer<T>, ctx: ToolContext) => Promise<ToolResult>

  // Optional metadata
  permissions?: Array<'read' | 'write' | 'delete' | 'execute'>
  locations?: (input: z.infer<T>) => string[]
  examples?: Array<{ input: z.infer<T>; description: string }>
}

export function defineTool<T extends z.ZodType>(config: ToolConfig<T>): Tool {
  const definition: ToolDefinition = {
    name: config.name,
    description: config.description,
    input_schema: zodToJsonSchema(config.schema, { target: 'openApi3' }),
  }

  return {
    definition,
    execute: async (rawInput: unknown, ctx: ToolContext): Promise<ToolResult> => {
      // Validate input
      const parsed = config.schema.safeParse(rawInput)

      if (!parsed.success) {
        return {
          success: false,
          output: formatZodError(parsed.error),
        }
      }

      // Execute with validated input
      const result = await config.execute(parsed.data, ctx)

      // Add location metadata if provided
      if (config.locations && result.metadata) {
        result.metadata.locations = config.locations(parsed.data)
      }

      return result
    },

    // Expose for permission system
    permissions: config.permissions,
    getLocations: config.locations,
  }
}

function formatZodError(error: z.ZodError): string {
  return error.errors
    .map(e => `${e.path.join('.')}: ${e.message}`)
    .join('\n')
}
```

### Example Tool Migration

```typescript
// BEFORE (glob.ts)
export const globTool: Tool<GlobInput> = {
  definition: {
    name: 'glob',
    description: 'Find files matching pattern',
    input_schema: {
      type: 'object',
      properties: {
        pattern: { type: 'string', description: '...' },
        path: { type: 'string', description: '...' },
      },
      required: ['pattern'],
    },
  },
  execute: async (input, ctx) => { ... },
}

// AFTER (glob.ts)
import { z } from 'zod'
import { defineTool } from './define.js'

const GlobSchema = z.object({
  pattern: z.string().describe('Glob pattern (e.g., "**/*.ts")'),
  path: z.string().optional().describe('Base directory'),
})

export const globTool = defineTool({
  name: 'glob',
  description: 'Find files matching pattern',
  schema: GlobSchema,
  permissions: ['read'],
  locations: (input) => [input.path ?? '.'],
  execute: async (input, ctx) => { ... },
})
```

---

## Sprint 6.2: Diff Tracking

### Files to Create

```
packages/core/src/diff/
├── types.ts          # PendingEdit, DiffHunk
├── unified.ts        # Create/parse unified diffs
├── tracker.ts        # Track pending edits
└── index.ts
```

### Dependencies

```bash
pnpm add diff -w --filter @ava/core
```

### Implementation

```typescript
// types.ts
export interface PendingEdit {
  id: string
  path: string
  original: string
  modified: string
  diff: string
  status: 'pending' | 'applied' | 'rejected'
  createdAt: number
}

export interface DiffHunk {
  oldStart: number
  oldLines: number
  newStart: number
  newLines: number
  lines: string[]
}

// unified.ts
import { createTwoFilesPatch, parsePatch } from 'diff'

export function createDiff(path: string, original: string, modified: string): string {
  return createTwoFilesPatch(
    `a/${path}`,
    `b/${path}`,
    original,
    modified,
    undefined,
    undefined,
    { context: 3 }
  )
}

export function parseDiff(diff: string): DiffHunk[] {
  const patches = parsePatch(diff)
  return patches.flatMap(p => p.hunks.map(h => ({
    oldStart: h.oldStart,
    oldLines: h.oldLines,
    newStart: h.newStart,
    newLines: h.newLines,
    lines: h.lines,
  })))
}

// tracker.ts
export class DiffTracker {
  private pending = new Map<string, PendingEdit>()

  add(path: string, original: string, modified: string): PendingEdit {
    const edit: PendingEdit = {
      id: `edit-${Date.now()}-${Math.random().toString(36).slice(2)}`,
      path,
      original,
      modified,
      diff: createDiff(path, original, modified),
      status: 'pending',
      createdAt: Date.now(),
    }
    this.pending.set(edit.id, edit)
    return edit
  }

  apply(id: string): PendingEdit | undefined {
    const edit = this.pending.get(id)
    if (edit) {
      edit.status = 'applied'
    }
    return edit
  }

  reject(id: string): PendingEdit | undefined {
    const edit = this.pending.get(id)
    if (edit) {
      edit.status = 'rejected'
    }
    return edit
  }

  getPending(): PendingEdit[] {
    return [...this.pending.values()].filter(e => e.status === 'pending')
  }

  getAll(): PendingEdit[] {
    return [...this.pending.values()]
  }
}
```

---

## Sprint 6.3: Git Snapshots

### Files to Create

```
packages/core/src/git/
├── types.ts          # Snapshot, GitConfig
├── snapshot.ts       # Create/restore snapshots
├── utils.ts          # Git command helpers
└── index.ts
```

### Implementation

```typescript
// types.ts
export interface Snapshot {
  id: string
  sha: string
  branch: string
  message: string
  paths: string[]
  createdAt: number
}

export interface GitConfig {
  enabled: boolean
  autoCommit: boolean
  branchPrefix: string  // e.g., 'ava/'
}

// snapshot.ts
import { getPlatform } from '../platform.js'

export async function createSnapshot(
  paths: string[],
  message: string,
  config: GitConfig
): Promise<Snapshot | null> {
  if (!config.enabled) return null

  const shell = getPlatform().shell

  // Check if in git repo
  const { exitCode } = await shell.exec('git rev-parse --git-dir')
  if (exitCode !== 0) return null

  // Get current branch and SHA
  const { stdout: branch } = await shell.exec('git branch --show-current')
  const { stdout: sha } = await shell.exec('git rev-parse HEAD')

  if (config.autoCommit && paths.length > 0) {
    // Stage and commit
    await shell.exec(`git add ${paths.map(p => `"${p}"`).join(' ')}`)
    await shell.exec(`git commit -m "${message}"`)
  }

  return {
    id: `snap-${Date.now()}`,
    sha: sha.trim(),
    branch: branch.trim(),
    message,
    paths,
    createdAt: Date.now(),
  }
}

export async function rollback(snapshot: Snapshot): Promise<boolean> {
  const shell = getPlatform().shell

  // Checkout the specific commit for affected paths
  const pathArgs = snapshot.paths.map(p => `"${p}"`).join(' ')
  const { exitCode } = await shell.exec(
    `git checkout ${snapshot.sha} -- ${pathArgs}`
  )

  return exitCode === 0
}

export async function getHistory(limit = 10): Promise<Snapshot[]> {
  const shell = getPlatform().shell

  const { stdout, exitCode } = await shell.exec(
    `git log --oneline -${limit} --format="%H|%s|%aI"`
  )

  if (exitCode !== 0) return []

  return stdout.trim().split('\n').map(line => {
    const [sha, message, date] = line.split('|')
    return {
      id: sha.slice(0, 8),
      sha,
      branch: '',  // Would need another call
      message,
      paths: [],
      createdAt: new Date(date).getTime(),
    }
  })
}
```

---

## Directory Ownership

This epic owns:
- `packages/core/src/tools/define.ts` (new)
- `packages/core/src/tools/validation.ts` (new)
- `packages/core/src/diff/` (new)
- `packages/core/src/git/` (new)

---

## Dependencies

- Epic 3 complete (ACP + Core)
- No dependencies on other infrastructure epics

---

## Acceptance Criteria

- [ ] Tool.define() wrapper works with Zod schemas
- [ ] All existing tools migrated to Tool.define()
- [ ] Diff tracker shows pending edits with unified diff format
- [ ] Git snapshots created before file modifications
- [ ] Rollback restores files to previous state
- [ ] No regressions in existing tool functionality
