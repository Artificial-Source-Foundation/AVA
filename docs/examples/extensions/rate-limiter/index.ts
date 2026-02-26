/**
 * Example: Rate Limiter Middleware Extension
 *
 * Demonstrates how to use addToolMiddleware() to intercept tool execution.
 * This middleware limits how many times a tool can be called per minute.
 */

import type { Disposable, ExtensionAPI, ToolMiddleware } from '@ava/core-v2/extensions'

const MAX_CALLS_PER_MINUTE = 30
const WINDOW_MS = 60_000

export function activate(api: ExtensionAPI): Disposable {
  const callLog: number[] = []

  const middleware: ToolMiddleware = {
    name: 'rate-limiter',
    priority: 5, // runs after permissions (0) but before hooks (10)

    async before(call) {
      // Clean up old entries
      const now = Date.now()
      while (callLog.length > 0 && callLog[0]! < now - WINDOW_MS) {
        callLog.shift()
      }

      // Check rate limit
      if (callLog.length >= MAX_CALLS_PER_MINUTE) {
        api.log.warn(`Rate limit exceeded: ${callLog.length} calls in last minute`)
        return {
          blocked: true,
          reason: `Rate limit exceeded (${MAX_CALLS_PER_MINUTE} calls/minute). Wait before retrying.`,
        }
      }

      callLog.push(now)
      return { blocked: false }
    },

    async after(_call, result) {
      // Pass through — no modification of results
      return result
    },
  }

  const disposable = api.addToolMiddleware(middleware)
  api.log.info(`Rate limiter active: max ${MAX_CALLS_PER_MINUTE} calls/minute`)
  return disposable
}
