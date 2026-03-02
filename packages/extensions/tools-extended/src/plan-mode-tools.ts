/**
 * Plan Mode Tools — thin wrappers around plan-mode state management.
 *
 * These tools let the agent enter/exit plan mode, which restricts
 * available tools to read-only operations.
 */

import { defineTool } from '@ava/core-v2/tools'
import { z } from 'zod'
import { enterPlanMode, exitPlanMode, isPlanModeEnabled } from '../../agent-modes/src/plan-mode.js'
import { savePlanToFile } from '../../agent-modes/src/plan-save.js'

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
  description:
    'Exit plan mode — restores full tool access. Optionally saves plan content to a file.',

  schema: z.object({
    plan: z.string().optional().describe('Plan content to save to .ava/plans/'),
    slug: z.string().optional().describe('Short name for the plan file'),
  }),

  permissions: ['read'],

  async execute(input, ctx) {
    if (!isPlanModeEnabled(ctx.sessionId)) {
      return {
        success: false,
        output: 'Not in plan mode',
        error: 'NOT_IN_PLAN_MODE',
      }
    }

    exitPlanMode(ctx.sessionId)

    // Save plan to file if content was provided
    if (input.plan) {
      try {
        const path = await savePlanToFile(input.plan, input.slug)
        return {
          success: true,
          output: `Exited plan mode. Plan saved to ${path}. All tools now available.`,
        }
      } catch {
        return {
          success: true,
          output: 'Exited plan mode. Failed to save plan file. All tools now available.',
        }
      }
    }

    return {
      success: true,
      output: 'Exited plan mode. All tools now available.',
    }
  },
})
