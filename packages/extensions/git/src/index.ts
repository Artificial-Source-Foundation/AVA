/**
 * Git extension — snapshots, auto-commit, git tools, and checkpoints.
 *
 * Takes snapshots before file modifications for easy rollback.
 * Registers middleware at priority 30 and a /snapshot command.
 * Provides git tools: create_pr, create_branch, switch_branch, read_issue.
 * Per-tool-call checkpoints via stash for rollback safety.
 * Graceful no-op if not in a git repository.
 */

import type {
  Disposable,
  ExtensionAPI,
  ToolMiddleware,
  ToolMiddlewareContext,
  ToolMiddlewareResult,
} from '@ava/core-v2/extensions'
import { createBranchTool, switchBranchTool } from './branch.js'
import { createCheckpointMiddleware } from './checkpoints.js'
import { readIssueTool } from './issue.js'
import { createPrTool } from './pr.js'
import { createSnapshotManager, isGitRepo } from './snapshots.js'
import type { GitConfig } from './types.js'
import { DEFAULT_GIT_CONFIG } from './types.js'

const FILE_WRITE_TOOLS = new Set([
  'write_file',
  'edit',
  'create_file',
  'apply_patch',
  'delete_file',
])

export function activate(api: ExtensionAPI): Disposable {
  const config = {
    ...DEFAULT_GIT_CONFIG,
    ...api.getSettings<Partial<GitConfig>>('git'),
  }
  const disposables: Disposable[] = []
  const manager = createSnapshotManager(api.platform.shell, config)
  let gitAvailable = false
  let cwd = ''

  // ── Git tools ───────────────────────────────────────────────────────────────
  disposables.push(api.registerTool(createPrTool))
  disposables.push(api.registerTool(createBranchTool))
  disposables.push(api.registerTool(switchBranchTool))
  disposables.push(api.registerTool(readIssueTool))

  // ── Per-tool-call checkpoints ───────────────────────────────────────────────
  const { middleware: checkpointMiddleware, store: checkpointStore } = createCheckpointMiddleware(
    api.platform.shell
  )
  disposables.push(api.addToolMiddleware(checkpointMiddleware))

  // Check git availability on session open
  disposables.push(
    api.on('session:opened', (data) => {
      const { workingDirectory } = data as { sessionId: string; workingDirectory: string }
      cwd = workingDirectory
      void isGitRepo(api.platform.shell, cwd).then((available) => {
        gitAvailable = available
        if (available) {
          api.log.debug('Git extension: repository detected')
          api.emit('git:ready', { cwd })
        } else {
          api.log.debug('Git extension: not a git repository, snapshots disabled')
        }
      })
    })
  )

  // Snapshot middleware (only active in git repos with snapshotOnToolCall)
  if (config.snapshotOnToolCall) {
    const middleware: ToolMiddleware = {
      name: 'ava-git-snapshots',
      priority: 30,

      async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
        if (!gitAvailable || !FILE_WRITE_TOOLS.has(ctx.toolName)) return undefined

        const filePath = (ctx.args.path ?? ctx.args.file_path) as string | undefined
        if (!filePath) return undefined

        await manager.createSnapshot(
          ctx.ctx.workingDirectory || cwd,
          `Before ${ctx.toolName}: ${filePath}`,
          [filePath]
        )
        return undefined
      },
    }
    disposables.push(api.addToolMiddleware(middleware))
  }

  // Register /snapshot command
  disposables.push(
    api.registerCommand({
      name: 'snapshot',
      description: 'Create a git snapshot of current changes',
      async execute(_args, ctx) {
        if (!gitAvailable) return 'Not in a git repository.'
        const snapshot = await manager.createSnapshot(
          ctx.workingDirectory || cwd,
          'Manual snapshot',
          []
        )
        return snapshot
          ? `Snapshot created: ${snapshot.hash.slice(0, 8)}`
          : 'No changes to snapshot.'
      },
    })
  )

  // Register /undo command — restores the most recent snapshot
  disposables.push(
    api.registerCommand({
      name: 'undo',
      description: 'Undo the most recent file change by restoring the latest git snapshot',
      async execute(_args, ctx) {
        if (!gitAvailable) return 'Not in a git repository.'
        const latest = manager.getLatestSnapshot()
        if (!latest) return 'No snapshots available to restore.'
        const dir = ctx.workingDirectory || cwd
        const restored = await manager.restoreSnapshot(dir, latest.hash)
        return restored
          ? `Restored snapshot ${latest.hash.slice(0, 8)}: ${latest.message}`
          : `Failed to restore snapshot ${latest.hash.slice(0, 8)}.`
      },
    })
  )

  api.log.debug('Git extension activated')

  return {
    dispose() {
      for (const d of disposables) d.dispose()
      manager.clear()
      checkpointStore.reset()
    },
  }
}
