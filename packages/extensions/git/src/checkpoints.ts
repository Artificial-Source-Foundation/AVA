/**
 * Per-tool-call checkpoints — creates git stash snapshots after modifying tools.
 *
 * Registers as middleware at priority 20 (after permissions at 0, hooks at 10).
 * Each successful modifying tool call creates a lightweight checkpoint via
 * `git stash create` + `git stash store` for potential rollback.
 */

import type {
  ToolMiddleware,
  ToolMiddlewareContext,
  ToolMiddlewareResult,
} from '@ava/core-v2/extensions'
import { createLogger } from '@ava/core-v2/logger'
import type { IShell } from '@ava/core-v2/platform'
import type { ToolResult } from '@ava/core-v2/tools'

const log = createLogger('Checkpoints')

/** Tools that modify the file system. */
const MODIFYING_TOOLS = new Set([
  'write_file',
  'edit',
  'create_file',
  'delete_file',
  'bash',
  'multiedit',
  'apply_patch',
])

export interface Checkpoint {
  id: number
  toolName: string
  timestamp: number
  stashRef: string
}

export interface CheckpointStore {
  getCheckpoints(): Checkpoint[]
  reset(): void
}

export function createCheckpointMiddleware(shell: IShell): {
  middleware: ToolMiddleware
  store: CheckpointStore
} {
  let counter = 0
  const checkpoints: Checkpoint[] = []

  const middleware: ToolMiddleware = {
    name: 'ava-checkpoints',
    priority: 20,

    async after(
      ctx: ToolMiddlewareContext,
      result: ToolResult
    ): Promise<ToolMiddlewareResult | undefined> {
      if (!MODIFYING_TOOLS.has(ctx.toolName)) return undefined
      if (!result.success) return undefined

      const cwd = ctx.ctx.workingDirectory
      if (!cwd) return undefined

      try {
        // Check if we're in a git repo
        const check = await shell.exec(`cd "${cwd}" && git rev-parse --is-inside-work-tree`)
        if (check.stdout.trim() !== 'true') return undefined

        counter++
        const label = `ava-checkpoint-${counter}-${ctx.toolName}`

        // Stage all changes, then create stash ref
        await shell.exec(`cd "${cwd}" && git add -A`)
        const stashCreate = await shell.exec(`cd "${cwd}" && git stash create "${label}"`)
        const stashRef = stashCreate.stdout.trim()

        if (!stashRef) {
          // No changes to stash (clean tree)
          return undefined
        }

        // Store the stash ref so it's accessible via git stash list
        await shell.exec(`cd "${cwd}" && git stash store -m "${label}" ${stashRef}`)

        checkpoints.push({
          id: counter,
          toolName: ctx.toolName,
          timestamp: Date.now(),
          stashRef,
        })

        log.debug(`Checkpoint ${counter} created after ${ctx.toolName}`)
      } catch (err) {
        log.debug(
          `Checkpoint failed (non-critical): ${err instanceof Error ? err.message : 'unknown'}`
        )
      }

      return undefined
    },
  }

  const store: CheckpointStore = {
    getCheckpoints() {
      return [...checkpoints]
    },
    reset() {
      checkpoints.length = 0
      counter = 0
    },
  }

  return { middleware, store }
}
