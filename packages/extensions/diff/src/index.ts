/**
 * Diff extension — tracks file changes during agent sessions.
 *
 * Registers tool middleware at priority 20 to snapshot files before
 * write_file/edit/delete_file operations and compute diffs afterward.
 * Provides undo/redo via diff:undo and diff:redo events.
 */

import type {
  Disposable,
  ExtensionAPI,
  ToolMiddleware,
  ToolMiddlewareContext,
  ToolMiddlewareResult,
} from '@ava/core-v2/extensions'
import type { ToolResult } from '@ava/core-v2/tools'
import { addDiff, createDiffSession, createFileDiff } from './tracker.js'
import type { DiffSession, FileDiff } from './types.js'

const FILE_WRITE_TOOLS = new Set([
  'write_file',
  'edit',
  'create_file',
  'apply_patch',
  'delete_file',
])

export function activate(api: ExtensionAPI): Disposable {
  const sessions = new Map<string, DiffSession>()
  const snapshots = new Map<string, string>()

  // Undo/redo stacks per session (LIFO)
  const undoStacks = new Map<string, FileDiff[]>()
  const redoStacks = new Map<string, FileDiff[]>()

  function getUndoStack(sessionId: string): FileDiff[] {
    let stack = undoStacks.get(sessionId)
    if (!stack) {
      stack = []
      undoStacks.set(sessionId, stack)
    }
    return stack
  }

  function getRedoStack(sessionId: string): FileDiff[] {
    let stack = redoStacks.get(sessionId)
    if (!stack) {
      stack = []
      redoStacks.set(sessionId, stack)
    }
    return stack
  }

  function getOrCreateSession(sessionId: string): DiffSession {
    let session = sessions.get(sessionId)
    if (!session) {
      session = createDiffSession(sessionId)
      sessions.set(sessionId, session)
    }
    return session
  }

  const middleware: ToolMiddleware = {
    name: 'ava-diff-tracker',
    priority: 20,

    async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
      if (!FILE_WRITE_TOOLS.has(ctx.toolName)) return undefined

      const filePath = (ctx.args.path ?? ctx.args.file_path) as string | undefined
      if (!filePath) return undefined

      try {
        const content = await api.platform.fs.readFile(filePath)
        snapshots.set(`${ctx.ctx.sessionId}:${filePath}`, content)
      } catch {
        // File doesn't exist yet — that's fine (create_file)
      }

      return undefined
    },

    async after(
      ctx: ToolMiddlewareContext,
      _result: ToolResult
    ): Promise<ToolMiddlewareResult | undefined> {
      if (!FILE_WRITE_TOOLS.has(ctx.toolName)) return undefined

      const filePath = (ctx.args.path ?? ctx.args.file_path) as string | undefined
      if (!filePath) return undefined

      const snapshotKey = `${ctx.ctx.sessionId}:${filePath}`
      const original = snapshots.get(snapshotKey)
      snapshots.delete(snapshotKey)

      try {
        const modified = await api.platform.fs.readFile(filePath)
        const diff = createFileDiff(filePath, original, modified)
        const session = getOrCreateSession(ctx.ctx.sessionId)
        addDiff(session, diff)

        // Push to undo stack + clear redo (standard undo/redo semantics)
        const undoStack = getUndoStack(ctx.ctx.sessionId)
        undoStack.push(diff)
        const redoStack = getRedoStack(ctx.ctx.sessionId)
        redoStack.length = 0

        api.emit('diff:changed', { sessionId: ctx.ctx.sessionId, diff })
      } catch {
        // File was deleted — record deletion diff if we had a snapshot
        if (original !== undefined) {
          const diff: FileDiff = { path: filePath, type: 'deleted', original, hunks: [] }
          const session = getOrCreateSession(ctx.ctx.sessionId)
          addDiff(session, diff)

          const undoStack = getUndoStack(ctx.ctx.sessionId)
          undoStack.push(diff)
          const redoStack = getRedoStack(ctx.ctx.sessionId)
          redoStack.length = 0

          api.emit('diff:changed', { sessionId: ctx.ctx.sessionId, diff })
        }
      }

      return undefined
    },
  }

  const mwDisposable = api.addToolMiddleware(middleware)

  // Wire diff:undo listener
  const undoDisposable = api.on('diff:undo', async (data: unknown) => {
    const { sessionId } = data as { sessionId: string }
    const undoStack = getUndoStack(sessionId)
    const diff = undoStack.pop()

    if (!diff) {
      api.emit('diff:undo-failed', { sessionId, reason: 'Nothing to undo' })
      return
    }

    // Restore based on diff type
    if (diff.type === 'modified' || diff.type === 'deleted') {
      await api.platform.fs.writeFile(diff.path, diff.original!)
    } else if (diff.type === 'added') {
      await api.platform.fs.remove(diff.path)
    }

    const redoStack = getRedoStack(sessionId)
    redoStack.push(diff)
    api.emit('diff:undone', { sessionId, diff })
  })

  // Wire diff:redo listener
  const redoDisposable = api.on('diff:redo', async (data: unknown) => {
    const { sessionId } = data as { sessionId: string }
    const redoStack = getRedoStack(sessionId)
    const diff = redoStack.pop()

    if (!diff) {
      api.emit('diff:redo-failed', { sessionId, reason: 'Nothing to redo' })
      return
    }

    // Re-apply based on diff type
    if (diff.type === 'modified' || diff.type === 'added') {
      await api.platform.fs.writeFile(diff.path, diff.modified!)
    } else if (diff.type === 'deleted') {
      await api.platform.fs.remove(diff.path)
    }

    const undoStack = getUndoStack(sessionId)
    undoStack.push(diff)
    api.emit('diff:redone', { sessionId, diff })
  })

  api.log.debug('Diff tracking extension activated')

  return {
    dispose() {
      mwDisposable.dispose()
      undoDisposable.dispose()
      redoDisposable.dispose()
      sessions.clear()
      snapshots.clear()
      undoStacks.clear()
      redoStacks.clear()
    },
  }
}
