/**
 * Delta9 Budget Enforcement
 *
 * Budget tracking and enforcement with configurable thresholds.
 */

import type { BudgetTracking, BudgetBreakdown } from '../types/mission.js'
import { loadConfig } from './config.js'

// =============================================================================
// Types
// =============================================================================

export interface BudgetConfig {
  /** Whether budget tracking is enabled */
  enabled: boolean
  /** Default budget limit in dollars */
  defaultLimit: number
  /** Warning threshold (0-1, e.g., 0.7 = 70%) */
  warnAt: number
  /** Pause threshold (0-1, e.g., 0.9 = 90%) */
  pauseAt: number
  /** Track by agent category */
  trackByAgent: boolean
}

export interface BudgetStatus {
  /** Whether budget tracking is enabled */
  enabled: boolean
  /** Budget limit */
  limit: number
  /** Total spent */
  spent: number
  /** Remaining budget */
  remaining: number
  /** Percentage used */
  percentage: number
  /** Whether warning threshold reached */
  isWarning: boolean
  /** Whether pause threshold reached */
  shouldPause: boolean
  /** Whether budget is exceeded */
  isExceeded: boolean
  /** Breakdown by category */
  breakdown: BudgetBreakdown
}

export interface BudgetCheckResult {
  /** Whether operation is allowed */
  allowed: boolean
  /** Reason if not allowed */
  reason?: string
  /** Warning message if near threshold */
  warning?: string
  /** Remaining budget after operation */
  remainingAfter: number
}

export type AgentCategory = keyof BudgetBreakdown

// =============================================================================
// Constants
// =============================================================================

/** Default budget configuration */
const DEFAULT_CONFIG: BudgetConfig = {
  enabled: true,
  defaultLimit: 10.0,
  warnAt: 0.7,
  pauseAt: 0.9,
  trackByAgent: true,
}

/** Cost estimates per model (per 1K tokens) */
export const MODEL_COSTS: Record<string, { input: number; output: number }> = {
  // Anthropic
  'anthropic/claude-opus-4': { input: 0.015, output: 0.075 },
  'anthropic/claude-sonnet-4-5': { input: 0.003, output: 0.015 },
  'anthropic/claude-haiku-4': { input: 0.00025, output: 0.00125 },
  // OpenAI
  'openai/gpt-4o': { input: 0.005, output: 0.015 },
  'openai/gpt-4o-mini': { input: 0.00015, output: 0.0006 },
  // Google
  'google/gemini-2.0-flash': { input: 0.0001, output: 0.0004 },
  'google/gemini-2.0-pro': { input: 0.00125, output: 0.005 },
  // DeepSeek
  'deepseek/deepseek-chat': { input: 0.00014, output: 0.00028 },
}

// =============================================================================
// Budget Manager
// =============================================================================

export class BudgetManager {
  private config: BudgetConfig
  private cwd: string

  constructor(cwd: string) {
    this.cwd = cwd
    this.config = this.loadConfig()
  }

  /**
   * Load budget config from project/global config
   */
  private loadConfig(): BudgetConfig {
    try {
      const config = loadConfig(this.cwd)
      return {
        enabled: config.budget?.enabled ?? DEFAULT_CONFIG.enabled,
        defaultLimit: config.budget?.defaultLimit ?? DEFAULT_CONFIG.defaultLimit,
        warnAt: config.budget?.warnAt ?? DEFAULT_CONFIG.warnAt,
        pauseAt: config.budget?.pauseAt ?? DEFAULT_CONFIG.pauseAt,
        trackByAgent: config.budget?.trackByAgent ?? DEFAULT_CONFIG.trackByAgent,
      }
    } catch {
      return DEFAULT_CONFIG
    }
  }

  /**
   * Get budget configuration
   */
  getConfig(): BudgetConfig {
    return this.config
  }

  /**
   * Get default budget limit
   */
  getDefaultLimit(): number {
    return this.config.defaultLimit
  }

  /**
   * Calculate status from budget tracking
   */
  getStatus(budget: BudgetTracking): BudgetStatus {
    const percentage = budget.limit > 0 ? (budget.spent / budget.limit) * 100 : 0

    return {
      enabled: this.config.enabled,
      limit: budget.limit,
      spent: budget.spent,
      remaining: Math.max(0, budget.limit - budget.spent),
      percentage: Math.round(percentage),
      isWarning: percentage >= this.config.warnAt * 100,
      shouldPause: percentage >= this.config.pauseAt * 100,
      isExceeded: budget.spent >= budget.limit,
      breakdown: budget.breakdown,
    }
  }

  /**
   * Check if an operation is within budget
   */
  checkBudget(budget: BudgetTracking, estimatedCost: number): BudgetCheckResult {
    if (!this.config.enabled) {
      return {
        allowed: true,
        remainingAfter: budget.limit - budget.spent - estimatedCost,
      }
    }

    const status = this.getStatus(budget)
    const remainingAfter = status.remaining - estimatedCost

    // Check if exceeded
    if (status.isExceeded) {
      return {
        allowed: false,
        reason: `Budget exceeded: $${budget.spent.toFixed(2)} / $${budget.limit.toFixed(2)}`,
        remainingAfter,
      }
    }

    // Check if this operation would exceed
    if (remainingAfter < 0) {
      return {
        allowed: false,
        reason: `Operation would exceed budget: estimated $${estimatedCost.toFixed(4)}, remaining $${status.remaining.toFixed(2)}`,
        remainingAfter,
      }
    }

    // Check pause threshold
    if (status.shouldPause) {
      return {
        allowed: false,
        reason: `Budget pause threshold reached (${this.config.pauseAt * 100}%): $${budget.spent.toFixed(2)} / $${budget.limit.toFixed(2)}`,
        remainingAfter,
      }
    }

    // Warning threshold
    if (status.isWarning) {
      return {
        allowed: true,
        warning: `Budget warning (${this.config.warnAt * 100}%): $${budget.spent.toFixed(2)} / $${budget.limit.toFixed(2)}`,
        remainingAfter,
      }
    }

    return {
      allowed: true,
      remainingAfter,
    }
  }

  /**
   * Estimate cost for a model call
   */
  estimateCost(model: string, inputTokens: number, outputTokens: number): number {
    const costs = MODEL_COSTS[model]

    if (!costs) {
      // Default to mid-range estimate
      return (inputTokens * 0.003 + outputTokens * 0.015) / 1000
    }

    return (inputTokens * costs.input + outputTokens * costs.output) / 1000
  }

  /**
   * Determine agent category from agent name
   */
  getAgentCategory(agentName: string): AgentCategory {
    const name = agentName.toLowerCase()

    // Council agents (CIPHER, VECTOR, PRISM, APEX)
    if (['cipher', 'vector', 'prism', 'apex', 'oracle'].some((o) => name.includes(o))) {
      return 'council'
    }

    // Validators
    if (name.includes('validator') || name.includes('validation')) {
      return 'validators'
    }

    // Support agents (Scout, Intel, Strategist, Scribe, Optics)
    if (['scout', 'intel', 'strategist', 'scribe', 'optics'].some((s) => name.includes(s))) {
      return 'support'
    }

    // Default to operators
    return 'operators'
  }
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Create a budget manager for a project
 */
export function createBudgetManager(cwd: string): BudgetManager {
  return new BudgetManager(cwd)
}

/**
 * Format budget for display
 */
export function formatBudget(budget: BudgetTracking): string {
  const percentage = budget.limit > 0 ? Math.round((budget.spent / budget.limit) * 100) : 0

  const lines: string[] = []

  lines.push(`Budget: $${budget.spent.toFixed(2)} / $${budget.limit.toFixed(2)} (${percentage}%)`)
  lines.push('')
  lines.push('Breakdown:')
  lines.push(`  Council:    $${budget.breakdown.council.toFixed(2)}`)
  lines.push(`  Operators:  $${budget.breakdown.operators.toFixed(2)}`)
  lines.push(`  Validators: $${budget.breakdown.validators.toFixed(2)}`)
  lines.push(`  Support:    $${budget.breakdown.support.toFixed(2)}`)

  // Progress bar
  const barLength = 20
  const filledLength = Math.round((percentage / 100) * barLength)
  const bar = '█'.repeat(filledLength) + '░'.repeat(barLength - filledLength)
  lines.push('')
  lines.push(`[${bar}] ${percentage}%`)

  return lines.join('\n')
}

/**
 * Describe budget status in human-readable format
 */
export function describeBudgetStatus(status: BudgetStatus): string {
  const lines: string[] = []

  if (!status.enabled) {
    return 'Budget tracking is disabled'
  }

  lines.push(`Spent: $${status.spent.toFixed(2)} / $${status.limit.toFixed(2)}`)
  lines.push(`Remaining: $${status.remaining.toFixed(2)} (${100 - status.percentage}%)`)

  if (status.isExceeded) {
    lines.push('⛔ BUDGET EXCEEDED')
  } else if (status.shouldPause) {
    lines.push('⚠️ PAUSE THRESHOLD REACHED')
  } else if (status.isWarning) {
    lines.push('⚠️ Warning threshold reached')
  } else {
    lines.push('✅ Within budget')
  }

  return lines.join('\n')
}
