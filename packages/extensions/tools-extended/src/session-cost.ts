import { defineTool } from '@ava/core-v2/tools'
import { z } from 'zod'

import { getSessionCost, resetSessionCost } from '../../context/src/cost-tracker.js'

const SessionCostSchema = z.object({
  sessionId: z.string().optional().describe('Session id to query. Defaults to current session'),
  reset: z
    .boolean()
    .optional()
    .describe('Reset tracked cost stats for the target session after reading'),
})

export const sessionCostTool = defineTool({
  name: 'session_cost',
  description: 'Get tracked token/cost totals for a session',
  schema: SessionCostSchema,
  permissions: ['read'],
  async execute(input, ctx) {
    const sessionId = input.sessionId ?? ctx.sessionId
    const stats = getSessionCost(sessionId)
    if (!stats) {
      return {
        success: false,
        output: '',
        error: `No session cost data for ${sessionId}`,
      }
    }

    const lines = [
      `Session: ${stats.sessionId}`,
      `Turns: ${stats.totalTurns}`,
      `Input tokens: ${stats.totalInputTokens}`,
      `Output tokens: ${stats.totalOutputTokens}`,
      `Total tokens: ${stats.totalTokens}`,
      `Total cost (USD): ${stats.totalCostUsd.toFixed(6)}`,
      '',
      'By model:',
    ]

    const modelKeys = Object.keys(stats.byModel).sort()
    for (const key of modelKeys) {
      const item = stats.byModel[key]
      if (!item) continue
      lines.push(
        `- ${item.provider}/${item.model}: turns=${item.turns} input=${item.inputTokens} output=${item.outputTokens} cost=${item.costUsd.toFixed(6)}`
      )
    }

    if (input.reset) {
      resetSessionCost(sessionId)
    }

    return {
      success: true,
      output: lines.join('\n'),
      metadata: stats as unknown as Record<string, unknown>,
    }
  },
})
