/**
 * Delta9 Budget Tools
 *
 * Tools for budget tracking and management.
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import { MissionState } from '../mission/state.js'
import {
  BudgetManager,
  formatBudget,
  describeBudgetStatus,
} from '../lib/budget.js'

// Use the tool's built-in schema
const s = tool.schema

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create budget tools
 */
export function createBudgetTools(cwd: string): Record<string, ToolDefinition> {
  const budgetManager = new BudgetManager(cwd)
  const missionState = new MissionState(cwd)

  /**
   * Get budget status
   */
  const budget_status = tool({
    description: `Get current mission budget status.

Shows:
- Total spent vs limit
- Breakdown by agent category (council, operators, validators, support)
- Warning/pause thresholds
- Visual progress bar`,

    args: {
      detailed: s.boolean().optional().describe('Show detailed breakdown'),
    },

    async execute(args, _ctx) {
      const mission = missionState.load()

      if (!mission) {
        return JSON.stringify({
          success: false,
          error: 'No active mission',
        })
      }

      const status = budgetManager.getStatus(mission.budget)

      if (args.detailed) {
        return JSON.stringify({
          success: true,
          status,
          config: budgetManager.getConfig(),
          humanReadable: formatBudget(mission.budget),
        })
      }

      return JSON.stringify({
        success: true,
        spent: status.spent,
        limit: status.limit,
        remaining: status.remaining,
        percentage: status.percentage,
        isWarning: status.isWarning,
        shouldPause: status.shouldPause,
        isExceeded: status.isExceeded,
        humanReadable: describeBudgetStatus(status),
      })
    },
  })

  /**
   * Set budget limit
   */
  const budget_set_limit = tool({
    description: `Set or update the budget limit for the current mission.

Use this to:
- Increase budget when needed
- Set a tighter limit for cost control`,

    args: {
      limit: s.number().describe('New budget limit in dollars'),
    },

    async execute(args, _ctx) {
      const mission = missionState.load()

      if (!mission) {
        return JSON.stringify({
          success: false,
          error: 'No active mission',
        })
      }

      if (args.limit <= 0) {
        return JSON.stringify({
          success: false,
          error: 'Budget limit must be positive',
        })
      }

      const oldLimit = mission.budget.limit
      missionState.updateMission({
        budget: {
          ...mission.budget,
          limit: args.limit,
        },
      })

      const newStatus = budgetManager.getStatus({
        ...mission.budget,
        limit: args.limit,
      })

      return JSON.stringify({
        success: true,
        oldLimit,
        newLimit: args.limit,
        spent: mission.budget.spent,
        newPercentage: newStatus.percentage,
        message: `Budget limit updated: $${oldLimit.toFixed(2)} → $${args.limit.toFixed(2)}`,
      })
    },
  })

  /**
   * Check if operation is within budget
   */
  const budget_check = tool({
    description: `Check if a planned operation is within budget.

Use before expensive operations to verify budget availability.`,

    args: {
      estimatedCost: s.number().describe('Estimated cost of the operation in dollars'),
      description: s.string().optional().describe('Description of the operation'),
    },

    async execute(args, _ctx) {
      const mission = missionState.load()

      if (!mission) {
        return JSON.stringify({
          success: false,
          error: 'No active mission',
        })
      }

      const result = budgetManager.checkBudget(mission.budget, args.estimatedCost)

      return JSON.stringify({
        success: true,
        allowed: result.allowed,
        reason: result.reason,
        warning: result.warning,
        estimatedCost: args.estimatedCost,
        currentSpent: mission.budget.spent,
        limit: mission.budget.limit,
        remainingAfter: result.remainingAfter,
      })
    },
  })

  /**
   * Get budget breakdown
   */
  const budget_breakdown = tool({
    description: `Get detailed budget breakdown by agent category.

Shows spending for:
- Council (CIPHER, VECTOR, PRISM, APEX)
- Operators (task execution)
- Validators (QA verification)
- Support (Scout, Intel, Strategist, etc.)`,

    args: {},

    async execute(_args, _ctx) {
      const mission = missionState.load()

      if (!mission) {
        return JSON.stringify({
          success: false,
          error: 'No active mission',
        })
      }

      const breakdown = mission.budget.breakdown
      const total = mission.budget.spent

      // Calculate percentages
      const percentages = {
        council: total > 0 ? Math.round((breakdown.council / total) * 100) : 0,
        operators: total > 0 ? Math.round((breakdown.operators / total) * 100) : 0,
        validators: total > 0 ? Math.round((breakdown.validators / total) * 100) : 0,
        support: total > 0 ? Math.round((breakdown.support / total) * 100) : 0,
      }

      return JSON.stringify({
        success: true,
        total,
        breakdown: {
          council: { amount: breakdown.council, percentage: percentages.council },
          operators: { amount: breakdown.operators, percentage: percentages.operators },
          validators: { amount: breakdown.validators, percentage: percentages.validators },
          support: { amount: breakdown.support, percentage: percentages.support },
        },
        humanReadable: formatBudget(mission.budget),
      })
    },
  })

  return {
    budget_status,
    budget_set_limit,
    budget_check,
    budget_breakdown,
  }
}

// =============================================================================
// Type Export
// =============================================================================

export type BudgetTools = ReturnType<typeof createBudgetTools>
