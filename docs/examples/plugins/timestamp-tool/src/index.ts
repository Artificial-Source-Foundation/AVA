/**
 * Timestamp Tool Plugin
 *
 * Demonstrates: registerTool(), defineTool(), Zod schema
 * Returns current date/time in ISO, unix, and human-readable formats.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'

const timestampTool = defineTool({
  name: 'timestamp',
  description: 'Get the current date and time in various formats.',
  schema: z.object({
    format: z
      .enum(['iso', 'unix', 'human', 'all'])
      .default('all')
      .describe('Output format: iso, unix, human, or all'),
  }),

  async execute(input, _ctx) {
    const now = new Date()
    const formats: Record<string, string> = {
      iso: now.toISOString(),
      unix: String(Math.floor(now.getTime() / 1000)),
      human: now.toLocaleString(),
    }

    if (input.format === 'all') {
      const lines = Object.entries(formats)
        .map(([key, val]) => `${key}: ${val}`)
        .join('\n')
      return { success: true, output: lines }
    }

    return { success: true, output: formats[input.format] }
  },
})

export function activate(api: ExtensionAPI): Disposable {
  const disposable = api.registerTool(timestampTool)
  api.log.info('Timestamp tool registered')
  return disposable
}
