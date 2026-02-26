/**
 * question tool — ask the user for clarification.
 */

import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'

export const questionTool = defineTool({
  name: 'question',
  description:
    'Ask the user one or more questions. Blocks until the user responds. Use when you need clarification.',
  schema: z.object({
    questions: z
      .array(
        z.object({
          text: z.string().describe('The question to ask'),
          options: z.array(z.string()).optional().describe('Predefined answer options'),
        })
      )
      .min(1)
      .max(4),
  }),
  async execute(input) {
    // In the full implementation, this would emit a question event and wait for
    // the user's response via the message bus. For now, we return the questions
    // as output for the agent to present.
    const output = input.questions
      .map((q, i) => {
        const opts = q.options ? `\nOptions: ${q.options.join(', ')}` : ''
        return `Question ${i + 1}: ${q.text}${opts}`
      })
      .join('\n\n')

    return {
      success: true,
      output,
      metadata: { requiresUserResponse: true, questions: input.questions },
    }
  },
})
