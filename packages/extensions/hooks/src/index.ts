/**
 * Hooks extension — lifecycle hooks as tool middleware.
 *
 * Integrates PreToolUse and PostToolUse hooks into the tool middleware chain
 * at priority 10 (after permissions at priority 0).
 */

import type {
  Disposable,
  ExtensionAPI,
  ToolMiddleware,
  ToolMiddlewareContext,
  ToolMiddlewareResult,
} from '@ava/core-v2/extensions'
import type { ToolResult } from '@ava/core-v2/tools'
import { runHooks } from './runner.js'
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
  const mwDisposable = api.addToolMiddleware(createHooksMiddleware())

  return {
    dispose() {
      mwDisposable.dispose()
    },
  }
}
