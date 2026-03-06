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
import type { ChatMessage } from '@ava/core-v2/llm'
import type { Tool, ToolResult } from '@ava/core-v2/tools'
import { HunkReviewState } from './hunk-review/state.js'
import type { HunkReviewStatus } from './hunk-review/types.js'
import { summarizeDiffSession } from './summary.js'
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
  const hunkReviewState = new HunkReviewState()
  const snapshots = new Map<string, string>()
  const toolCallCounters = new Map<string, number>()

  // Undo/redo stacks per session (LIFO)
  const undoStacks = new Map<string, FileDiff[]>()
  const redoStacks = new Map<string, FileDiff[]>()

  // Store removed messages keyed by `sessionId:messageIndex` for redo restoration
  const removedMessages = new Map<string, ChatMessage>()

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

  function emitHunkReviewUpdate(sessionId: string): void {
    api.emit('diff:hunks-updated', {
      sessionId,
      items: hunkReviewState.list(sessionId),
      summary: hunkReviewState.summary(sessionId),
    })
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

      // Track tool call index per session
      const counter = (toolCallCounters.get(ctx.ctx.sessionId) ?? 0) + 1
      toolCallCounters.set(ctx.ctx.sessionId, counter)

      // Determine the message index of the assistant message that caused this change
      let messageIndex: number | undefined
      try {
        const sessionMgr = api.getSessionManager()
        const sessionState = sessionMgr.get(ctx.ctx.sessionId)
        if (sessionState && sessionState.messages.length > 0) {
          messageIndex = sessionState.messages.length - 1
        }
      } catch {
        // Session manager not available — skip message tracking
      }

      try {
        const modified = await api.platform.fs.readFile(filePath)
        const diff = createFileDiff(filePath, original, modified)
        diff.toolCallIndex = counter
        diff.messageIndex = messageIndex
        const session = getOrCreateSession(ctx.ctx.sessionId)
        addDiff(session, diff)
        hunkReviewState.ingest(ctx.ctx.sessionId, diff)
        emitHunkReviewUpdate(ctx.ctx.sessionId)

        // Push to undo stack + clear redo (standard undo/redo semantics)
        const undoStack = getUndoStack(ctx.ctx.sessionId)
        undoStack.push(diff)
        const redoStack = getRedoStack(ctx.ctx.sessionId)
        redoStack.length = 0

        api.emit('diff:changed', { sessionId: ctx.ctx.sessionId, diff })
      } catch {
        // File was deleted — record deletion diff if we had a snapshot
        if (original !== undefined) {
          const diff: FileDiff = {
            path: filePath,
            type: 'deleted',
            original,
            hunks: [],
            toolCallIndex: counter,
            messageIndex,
          }
          const session = getOrCreateSession(ctx.ctx.sessionId)
          addDiff(session, diff)
          hunkReviewState.ingest(ctx.ctx.sessionId, diff)
          emitHunkReviewUpdate(ctx.ctx.sessionId)

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

  const diffReviewTool: Tool = {
    definition: {
      name: 'diff_review',
      description: 'Review tracked hunks and update hunk decisions',
      input_schema: {
        type: 'object',
        properties: {
          action: {
            type: 'string',
            enum: ['list', 'accept', 'reject', 'apply'],
            description: 'Review action to perform',
          },
          hunkId: { type: 'string', description: 'Hunk id for accept/reject actions' },
          path: { type: 'string', description: 'Optional file path filter for list action' },
        },
        required: ['action'],
      },
    },
    async execute(input, ctx) {
      const args = input as { action?: string; hunkId?: string; path?: string }
      const action = args.action ?? 'list'
      const sessionId = ctx.sessionId

      if (action === 'list') {
        const items = hunkReviewState.list(sessionId, args.path)
        return {
          success: true,
          output: `Found ${items.length} hunks`,
          metadata: { items, summary: hunkReviewState.summary(sessionId) },
        }
      }

      if (action === 'accept' || action === 'reject') {
        if (!args.hunkId) {
          return { success: false, output: 'hunkId is required for accept/reject' }
        }
        const status: HunkReviewStatus = action === 'accept' ? 'accepted' : 'rejected'
        const updated = hunkReviewState.updateStatus(sessionId, args.hunkId, status)
        if (!updated) {
          return { success: false, output: `Hunk not found: ${args.hunkId}` }
        }
        emitHunkReviewUpdate(sessionId)
        return {
          success: true,
          output: `Marked ${args.hunkId} as ${status}`,
          metadata: { summary: hunkReviewState.summary(sessionId) },
        }
      }

      if (action === 'apply') {
        const summary = hunkReviewState.summary(sessionId)
        return {
          success: true,
          output: `Applied review state for ${summary.accepted} accepted hunks (${summary.pending} pending).`,
          metadata: { summary },
        }
      }

      return { success: false, output: `Unknown action: ${action}` }
    },
  }

  const diffReviewDisposable = api.registerTool(diffReviewTool)

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

    // Remove the assistant message that caused this change
    if (diff.messageIndex !== undefined) {
      try {
        const sessionMgr = api.getSessionManager()
        const sessionState = sessionMgr.get(sessionId)
        if (sessionState && diff.messageIndex < sessionState.messages.length) {
          const removed = sessionState.messages[diff.messageIndex]!
          removedMessages.set(`${sessionId}:${diff.messageIndex}`, removed)
          const updated = [
            ...sessionState.messages.slice(0, diff.messageIndex),
            ...sessionState.messages.slice(diff.messageIndex + 1),
          ]
          sessionMgr.setMessages(sessionId, updated)
        }
      } catch {
        // Session manager not available — skip message removal
      }
    }

    const redoStack = getRedoStack(sessionId)
    redoStack.push(diff)
    api.emit('diff:undone', { sessionId, diff })
    emitHunkReviewUpdate(sessionId)
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

    // Restore the previously removed assistant message
    if (diff.messageIndex !== undefined) {
      const key = `${sessionId}:${diff.messageIndex}`
      const removed = removedMessages.get(key)
      if (removed) {
        try {
          const sessionMgr = api.getSessionManager()
          const sessionState = sessionMgr.get(sessionId)
          if (sessionState) {
            const updated = [...sessionState.messages]
            // Splice the message back at its original index
            updated.splice(diff.messageIndex, 0, removed)
            sessionMgr.setMessages(sessionId, updated)
          }
        } catch {
          // Session manager not available — skip message restoration
        }
        removedMessages.delete(key)
      }
    }

    const undoStack = getUndoStack(sessionId)
    undoStack.push(diff)
    api.emit('diff:redone', { sessionId, diff })
    emitHunkReviewUpdate(sessionId)
  })

  // Wire diff:revert-to listener — reverts all diffs after a given index
  const revertToDisposable = api.on('diff:revert-to', async (data: unknown) => {
    const { sessionId, index } = data as { sessionId: string; index: number }
    const undoStack = getUndoStack(sessionId)

    if (undoStack.length === 0) {
      api.emit('diff:revert-to-failed', { sessionId, reason: 'Nothing to revert' })
      return
    }

    const reverted: FileDiff[] = []
    while (undoStack.length > index) {
      const diff = undoStack.pop()!
      if (diff.type === 'modified' || diff.type === 'deleted') {
        await api.platform.fs.writeFile(diff.path, diff.original!)
      } else if (diff.type === 'added') {
        await api.platform.fs.remove(diff.path)
      }
      reverted.push(diff)
    }

    // Clear redo stack when reverting
    const redoStack = getRedoStack(sessionId)
    redoStack.length = 0

    api.emit('diff:reverted-to', { sessionId, index, reverted })
    emitHunkReviewUpdate(sessionId)
  })

  const hunkStatusDisposable = api.on('diff:hunk-status:update', (data: unknown) => {
    const payload = data as {
      sessionId?: string
      hunkId?: string
      status?: HunkReviewStatus
    }
    if (!payload.sessionId || !payload.hunkId || !payload.status) return
    const updated = hunkReviewState.updateStatus(payload.sessionId, payload.hunkId, payload.status)
    if (!updated) {
      api.emit('diff:hunk-status:update-failed', {
        sessionId: payload.sessionId,
        hunkId: payload.hunkId,
        status: payload.status,
      })
      return
    }
    emitHunkReviewUpdate(payload.sessionId)
  })

  // Wire agent:finish listener — compute and emit session summary
  const finishDisposable = api.on('agent:finish', (data: unknown) => {
    const event = data as { sessionId?: string; agentId?: string }
    const sessionId = event.sessionId ?? event.agentId
    if (!sessionId) return
    const session = sessions.get(sessionId)
    if (!session || session.diffs.length === 0) return

    const summary = summarizeDiffSession(session)
    api.emit('diff:session-summary', { sessionId, summary })
  })

  api.log.debug('Diff tracking extension activated')

  return {
    dispose() {
      mwDisposable.dispose()
      diffReviewDisposable.dispose()
      undoDisposable.dispose()
      redoDisposable.dispose()
      revertToDisposable.dispose()
      hunkStatusDisposable.dispose()
      finishDisposable.dispose()
      sessions.clear()
      snapshots.clear()
      undoStacks.clear()
      redoStacks.clear()
      toolCallCounters.clear()
      removedMessages.clear()
      hunkReviewState.clear()
    },
  }
}
