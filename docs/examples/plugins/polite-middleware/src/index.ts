/**
 * Polite Middleware Plugin
 *
 * Demonstrates: addToolMiddleware(), priority
 * Adds a gentle reminder to the output of all write operations.
 */

import type { Disposable, ExtensionAPI, ToolMiddleware } from '@ava/core-v2/extensions'

const WRITE_TOOLS = new Set(['write_file', 'edit', 'create_file'])
const REMINDER = '\n---\nReminder: Changes were saved. Please review before committing.'

const politeMiddleware: ToolMiddleware = {
  name: 'polite-reminder',
  priority: 100, // Low priority — runs after other middleware

  async after(ctx, result) {
    if (!WRITE_TOOLS.has(ctx.toolName) || !result.success) {
      return undefined
    }

    return {
      result: {
        ...result,
        output: result.output + REMINDER,
      },
    }
  },
}

export function activate(api: ExtensionAPI): Disposable {
  const disposable = api.addToolMiddleware(politeMiddleware)
  api.log.info('Polite middleware activated')
  return disposable
}
