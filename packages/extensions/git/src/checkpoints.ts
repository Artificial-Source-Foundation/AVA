/**
 * Per-tool-call checkpoints — creates git stash snapshots after modifying tools.
 *
 * Registers as middleware at priority 20 (after permissions at 0, hooks at 10).
 * Each successful modifying tool call creates a detached checkpoint commit via
 * `git commit-tree` and stores it under `refs/ava/checkpoints/<sha>`.
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
  ref: string
  commit: string
}

export interface CheckpointStore {
  getCheckpoints(): Checkpoint[]
  reset(): void
}

export function createCheckpointMiddleware(shell: IShell): {
  middleware: ToolMiddleware
  store: CheckpointStore
  createCheckpoint: (cwd: string, toolName: string) => Promise<void>
} {
  let counter = 0
  const checkpoints: Checkpoint[] = []

  const createCheckpoint = async (cwd: string, toolName: string): Promise<void> => {
    // Check if we're in a git repo
    const check = await shell.exec(`cd "${cwd}" && git rev-parse --is-inside-work-tree`)
    if (check.stdout.trim() !== 'true') return

    const nextCounter = counter + 1
    const label = `ava-checkpoint-${nextCounter}-${toolName}`

    await shell.exec(`cd "${cwd}" && git add -A`)

    const parentCommitResult = await shell.exec(`cd "${cwd}" && git rev-parse --verify HEAD`)
    const parentCommit = parentCommitResult.exitCode === 0 ? parentCommitResult.stdout.trim() : ''

    const treeResult = await shell.exec(`cd "${cwd}" && git write-tree`)
    const tree = treeResult.stdout.trim()
    if (!tree) {
      return
    }

    if (parentCommit) {
      const parentTreeResult = await shell.exec(
        `cd "${cwd}" && git rev-parse "${parentCommit}^{tree}"`
      )
      const parentTree = parentTreeResult.stdout.trim()
      if (parentTree && parentTree === tree) {
        return
      }
    }

    const commitTreeCmd = parentCommit
      ? `cd "${cwd}" && git commit-tree "${tree}" -p "${parentCommit}" -m "${label}"`
      : `cd "${cwd}" && git commit-tree "${tree}" -m "${label}"`

    const commitResult = await shell.exec(commitTreeCmd)
    const commit = commitResult.stdout.trim()
    if (!commit) {
      return
    }

    const ref = `refs/ava/checkpoints/${commit}`

    await shell.exec(`cd "${cwd}" && git update-ref "${ref}" "${commit}"`)

    counter = nextCounter

    checkpoints.push({
      id: counter,
      toolName,
      timestamp: Date.now(),
      ref,
      commit,
    })

    log.debug(`Checkpoint ${counter} created after ${toolName}`)
  }

  const middleware: ToolMiddleware = {
    name: 'ava-checkpoints',
    priority: 20,

    async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
      if (ctx.toolName !== 'bash') return undefined
      const command = typeof ctx.args.command === 'string' ? ctx.args.command : ''
      if (!/(rm\s+-rf|git\s+clean\s+-fd|mv\s+|chmod\s+|chown\s+)/.test(command)) {
        return undefined
      }

      try {
        await createCheckpoint(ctx.ctx.workingDirectory, 'pre-destructive-bash')
      } catch (err) {
        log.debug(
          `Pre-destructive checkpoint failed (non-critical): ${err instanceof Error ? err.message : 'unknown'}`
        )
      }

      return undefined
    },

    async after(
      ctx: ToolMiddlewareContext,
      result: ToolResult
    ): Promise<ToolMiddlewareResult | undefined> {
      if (!MODIFYING_TOOLS.has(ctx.toolName)) return undefined
      if (!result.success) return undefined

      const cwd = ctx.ctx.workingDirectory
      if (!cwd) return undefined

      try {
        await createCheckpoint(cwd, ctx.toolName)
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

  return { middleware, store, createCheckpoint }
}
