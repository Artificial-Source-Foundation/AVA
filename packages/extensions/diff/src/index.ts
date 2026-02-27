/**
 * Diff extension — tracks file changes during agent sessions.
 *
 * Registers tool middleware at priority 20 to snapshot files before
 * write_file/edit operations and compute diffs afterward.
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
import type { DiffSession } from './types.js'

const FILE_WRITE_TOOLS = new Set(['write_file', 'edit', 'create_file', 'apply_patch'])

export function activate(api: ExtensionAPI): Disposable {
  const sessions = new Map<string, DiffSession>()
  const snapshots = new Map<string, string>()

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
        api.emit('diff:changed', { sessionId: ctx.ctx.sessionId, diff })
      } catch {
        // File was deleted after write — unlikely but possible
      }

      return undefined
    },
  }

  const mwDisposable = api.addToolMiddleware(middleware)
  api.log.debug('Diff tracking extension activated')

  return {
    dispose() {
      mwDisposable.dispose()
      sessions.clear()
      snapshots.clear()
    },
  }
}
