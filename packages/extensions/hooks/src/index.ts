/**
 * Hooks extension — lifecycle hooks as tool middleware.
 *
 * Integrates PreToolUse and PostToolUse hooks into the tool middleware chain
 * at priority 10 (after permissions at priority 0).
 * Also registers a formatter middleware (priority 50) on session:opened.
 */

import type {
  Disposable,
  ExtensionAPI,
  ToolMiddleware,
  ToolMiddlewareContext,
  ToolMiddlewareResult,
} from '@ava/core-v2/extensions'
import type { ToolResult } from '@ava/core-v2/tools'
import { createErrorRecoveryMiddleware } from './error-recovery-middleware.js'
import { activate as activateFileWatcher } from './file-watcher/index.js'
import { createFormatterMiddleware } from './formatter.js'
import { createFormatterDetectionMiddleware } from './formatter-detection.js'
import { createProgressiveEscalationMiddleware } from './progressive-escalation.js'
import { runHooks } from './runner.js'
import { activate as activateScheduler } from './scheduler/index.js'
import type { PostToolUseContext, PreToolUseContext } from './types.js'

export function createHooksMiddleware(): ToolMiddleware {
  return {
    name: 'ava-hooks',
    priority: 10,

    async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
      const hookCtx: PreToolUseContext = {
        toolName: ctx.toolName,
        parameters: ctx.args,
        workingDirectory: ctx.ctx.workingDirectory,
        sessionId: ctx.ctx.sessionId,
      }

      const result = await runHooks('PreToolUse', hookCtx)

      if (result.cancel) {
        return { blocked: true, reason: result.errorMessage ?? 'Blocked by PreToolUse hook' }
      }

      return undefined
    },

    async after(
      ctx: ToolMiddlewareContext,
      result: ToolResult
    ): Promise<{ result?: ToolResult } | undefined> {
      const hookCtx: PostToolUseContext = {
        toolName: ctx.toolName,
        parameters: ctx.args,
        result: result.output,
        success: result.success,
        durationMs: 0,
        workingDirectory: ctx.ctx.workingDirectory,
        sessionId: ctx.ctx.sessionId,
      }

      await runHooks('PostToolUse', hookCtx)
      return undefined
    },
  }
}

export function activate(api: ExtensionAPI): Disposable {
  const disposables: Disposable[] = []
  disposables.push(activateFileWatcher(api))
  disposables.push(activateScheduler(api))

  disposables.push(api.addToolMiddleware(createHooksMiddleware()))
  disposables.push(api.addToolMiddleware(createProgressiveEscalationMiddleware(api, api.log)))
  disposables.push(api.addToolMiddleware(createErrorRecoveryMiddleware(api.platform, api.log)))

  // Register formatter middleware when a session opens
  disposables.push(
    api.on('session:opened', () => {
      const fmtMiddleware = createFormatterMiddleware(api.platform, api.log)
      const fmtDetectionMiddleware = createFormatterDetectionMiddleware(api.platform, api.log)
      disposables.push(api.addToolMiddleware(fmtMiddleware))
      disposables.push(api.addToolMiddleware(fmtDetectionMiddleware))
      api.log.debug('Formatter middleware registered')
    })
  )

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}
