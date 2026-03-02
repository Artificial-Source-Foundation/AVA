/**
 * Tool registry with middleware chain.
 *
 * Core registry is simple: register/get/execute. Policy, hooks, doom loop,
 * and approval are all handled by extension middleware.
 */

import { emitEvent, getToolMiddlewares } from '../extensions/api.js'
import type { ToolMiddlewareContext } from '../extensions/types.js'
import type { ToolDefinition } from '../llm/types.js'
import { createLogger } from '../logger/logger.js'
import { ToolError } from './errors.js'
import type { AnyTool, Tool, ToolContext, ToolResult } from './types.js'

const log = createLogger('ToolRegistry')

// ─── Registry ────────────────────────────────────────────────────────────────

const tools = new Map<string, AnyTool>()

export function registerTool(tool: AnyTool): void {
  emitEvent('tool:before-register', { name: tool.definition.name, definition: tool.definition })
  tools.set(tool.definition.name, tool)
  emitEvent('tools:registered', { name: tool.definition.name, definition: tool.definition })
  log.debug(`Tool registered: ${tool.definition.name}`)
}

export function unregisterTool(name: string): void {
  tools.delete(name)
  emitEvent('tools:unregistered', { name })
}

export function getTool(name: string): Tool | undefined {
  return tools.get(name)
}

export function getAllTools(): Tool[] {
  return [...tools.values()]
}

export function getToolDefinitions(): ToolDefinition[] {
  return [...tools.values()].map((t) => t.definition)
}

export function resetTools(): void {
  tools.clear()
}

// ─── Execution ───────────────────────────────────────────────────────────────

/**
 * Execute a tool by name with full middleware chain.
 *
 * Pipeline:
 * 1. Resolve tool
 * 2. Run `before` middlewares (sorted by priority)
 *    - Any middleware can block execution
 *    - Middlewares can modify args
 * 3. Validate params
 * 4. Execute tool
 * 5. Run `after` middlewares (sorted by priority)
 *    - Middlewares can modify result
 * 6. Return result
 */
export async function executeTool(
  name: string,
  args: Record<string, unknown>,
  ctx: ToolContext
): Promise<ToolResult> {
  const tool = tools.get(name)
  if (!tool) {
    return {
      success: false,
      output: '',
      error: `Unknown tool: ${name}`,
    }
  }

  const middlewareCtx: ToolMiddlewareContext = {
    toolName: name,
    args,
    ctx,
    definition: tool.definition,
  }

  emitEvent('tool:start', { name, args })
  const startTime = Date.now()

  try {
    // Run before-middlewares
    let currentArgs = args
    const middlewares = getToolMiddlewares()

    for (const mw of middlewares) {
      if (mw.before) {
        const result = await mw.before({ ...middlewareCtx, args: currentArgs })
        if (result?.blocked) {
          log.debug(`Tool blocked by middleware ${mw.name}: ${result.reason}`)
          emitEvent('tool:blocked', { name, middleware: mw.name, reason: result.reason })
          return {
            success: false,
            output: '',
            error: result.reason ?? `Blocked by ${mw.name}`,
          }
        }
        if (result?.args) {
          currentArgs = result.args
        }
      }
    }

    // Validate
    let validatedParams = currentArgs
    if (tool.validate) {
      try {
        validatedParams = tool.validate(currentArgs)
      } catch (err) {
        const error = ToolError.from(err, name)
        return { success: false, output: '', error: error.message }
      }
    }

    // Execute
    let result = await tool.execute(validatedParams, ctx)

    // Run after-middlewares
    for (const mw of middlewares) {
      if (mw.after) {
        const modified = await mw.after({ ...middlewareCtx, args: currentArgs }, result)
        if (modified?.result) {
          result = modified.result
        }
      }
    }

    const durationMs = Date.now() - startTime
    emitEvent('tool:finish', { name, durationMs, success: result.success })
    return result
  } catch (err) {
    const durationMs = Date.now() - startTime
    const error = ToolError.from(err, name)
    emitEvent('tool:error', { name, durationMs, error: error.message })
    return {
      success: false,
      output: '',
      error: error.message,
    }
  }
}
