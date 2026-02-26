/**
 * batch tool — execute multiple tools in parallel.
 */

import { defineTool, executeTool } from '@ava/core-v2/tools'
import * as z from 'zod'

export const batchTool = defineTool({
  name: 'batch',
  description:
    'Execute multiple tool calls in parallel. Returns aggregated results. Max 25 calls per batch.',
  schema: z.object({
    tool_calls: z
      .array(
        z.object({
          tool: z.string().describe('Tool name'),
          parameters: z.record(z.string(), z.unknown()).describe('Tool parameters'),
        })
      )
      .max(25),
  }),
  permissions: ['read', 'write', 'execute'],
  async execute(input, ctx) {
    if (input.tool_calls.length === 0) {
      return { success: false, output: '', error: 'No tool calls provided' }
    }

    // Prevent recursive batch calls
    if (input.tool_calls.some((tc) => tc.tool === 'batch')) {
      return { success: false, output: '', error: 'Cannot nest batch calls' }
    }

    const results = await Promise.all(
      input.tool_calls.map((tc) =>
        executeTool(tc.tool, tc.parameters as Record<string, unknown>, ctx)
      )
    )

    const output = results
      .map((r, i) => {
        const tc = input.tool_calls[i]
        const status = r.success ? 'OK' : 'ERROR'
        const content = r.success ? r.output : r.error
        return `[${status}] ${tc!.tool}: ${content}`
      })
      .join('\n\n')

    const allSuccess = results.every((r) => r.success)
    return { success: allSuccess, output }
  },
})
