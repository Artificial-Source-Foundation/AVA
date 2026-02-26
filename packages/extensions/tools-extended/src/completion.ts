/**
 * attempt_completion tool — signals task completion.
 */

import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'

export const completionTool = defineTool({
  name: 'attempt_completion',
  description:
    'Signal that the task is complete. Provide a summary of what was accomplished. Only call this when all changes have been verified and the task is fully done.',
  schema: z.object({
    result: z.string().describe('Summary of what was accomplished'),
    command: z.string().optional().describe('Optional command for user to verify'),
  }),
  async execute(input) {
    return {
      success: true,
      output: input.result,
      metadata: {
        completed: true,
        command: input.command,
      },
    }
  },
})
