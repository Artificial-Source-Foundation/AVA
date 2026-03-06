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
import { ShadowSnapshotManager } from './shadow-snapshots.js'
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
  let shadowManager: ShadowSnapshotManager | undefined
  let gitAvailable = false
  let cwd = ''

  function extractPath(args: Record<string, unknown>): string | undefined {
    const filePath = args.path ?? args.file_path ?? args.filePath
    return typeof filePath === 'string' ? filePath : undefined
  }

  // ── Git tools ───────────────────────────────────────────────────────────────
  disposables.push(api.registerTool(createPrTool))
  disposables.push(api.registerTool(createBranchTool))
  disposables.push(api.registerTool(switchBranchTool))
  disposables.push(api.registerTool(readIssueTool))

  // ── Per-tool-call checkpoints ───────────────────────────────────────────────
  const {
    middleware: checkpointMiddleware,
    store: checkpointStore,
    createCheckpoint,
  } = createCheckpointMiddleware(api.platform.shell)
  disposables.push(api.addToolMiddleware(checkpointMiddleware))

  // Check git availability on session open
  disposables.push(
    api.on('session:opened', (data) => {
      const { workingDirectory } = data as { sessionId: string; workingDirectory: string }
      cwd = workingDirectory
      void isGitRepo(api.platform.shell, cwd).then(async (available) => {
        gitAvailable = available
        if (available) {
          shadowManager = new ShadowSnapshotManager(cwd)
          await shadowManager.init()
          api.log.debug('Git extension: repository detected')
          api.emit('git:ready', { cwd })
          await createCheckpoint(cwd, 'session-opened')
        } else {
          api.log.debug('Git extension: not a git repository, snapshots disabled')
        }
      })
    })
  )

  // Snapshot middleware (only active in git repos with snapshotOnToolCall)
  if (config.snapshotOnToolCall) {
    const shadowMiddleware: ToolMiddleware = {
      name: 'ava-shadow-snapshots',
      priority: 25,

      async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
        if (!gitAvailable || !shadowManager || !FILE_WRITE_TOOLS.has(ctx.toolName)) return undefined

        const filePath = extractPath(ctx.args)
        if (!filePath) return undefined

        const isDestructive =
          ctx.toolName === 'delete_file' ||
          ctx.toolName === 'edit' ||
          ctx.toolName === 'apply_patch' ||
          ctx.toolName === 'write_file'
        if (!isDestructive) return undefined

        if (ctx.toolName === 'write_file') {
          const exists = await api.platform.fs.exists(filePath)
          if (!exists) return undefined
        }

        await shadowManager.take(
          ctx.ctx.sessionId,
          `Auto snapshot before ${ctx.toolName}: ${filePath}`
        )

        return undefined
      },
    }
    disposables.push(api.addToolMiddleware(shadowMiddleware))

    const middleware: ToolMiddleware = {
      name: 'ava-git-snapshots',
      priority: 30,

      async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
        if (!gitAvailable || !FILE_WRITE_TOOLS.has(ctx.toolName)) return undefined

        const filePath = extractPath(ctx.args)
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

  disposables.push(
    api.on('agent:finish', (data) => {
      const { sessionId } = data as { sessionId?: string }
      if (!sessionId || !shadowManager) return
      void shadowManager.prune(10)
    })
  )

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
