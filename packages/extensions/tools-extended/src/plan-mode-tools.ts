/**
 * Plan Mode Tools — thin wrappers around plan-mode state management.
 *
 * These tools let the agent enter/exit plan mode, which restricts
 * available tools to read-only operations.
 */

import { defineTool } from '@ava/core-v2/tools'
import { z } from 'zod'
import { enterPlanMode, exitPlanMode, isPlanModeEnabled } from '../../agent-modes/src/plan-mode.js'

export const planEnterTool = defineTool({
  name: 'plan_enter',
  description: 'Enter plan mode — restricts to read-only tools for research and planning.',

  schema: z.object({
    reason: z.string().optional().describe('Why entering plan mode'),
  }),

  permissions: ['read'],

  async execute(input, ctx) {
    if (isPlanModeEnabled(ctx.sessionId)) {
      return {
        success: false,
        output: 'Already in plan mode',
        error: 'ALREADY_IN_PLAN_MODE',
      }
    }

    enterPlanMode(ctx.sessionId, input.reason)
    return {
      success: true,
      output: 'Entered plan mode. Call plan_exit when ready to execute.',
    }
  },
})

export const planExitTool = defineTool({
  name: 'plan_exit',
  description: 'Exit plan mode — restores full tool access.',

  schema: z.object({}),

  permissions: ['read'],

  async execute(_input, ctx) {
    if (!isPlanModeEnabled(ctx.sessionId)) {
      return {
        success: false,
        output: 'Not in plan mode',
        error: 'NOT_IN_PLAN_MODE',
      }
    }

    exitPlanMode(ctx.sessionId)
    return {
      success: true,
      output: 'Exited plan mode. All tools now available.',
    }
  },
})
