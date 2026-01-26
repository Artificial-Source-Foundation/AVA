/**
 * Delta9 Three-Strike System
 *
 * Error escalation system for agents:
 * - Track consecutive failures per agent
 * - Escalate to human after 3 strikes
 * - Auto-decay strikes after timeout
 * - Provide guidance on retries
 */

import { nanoid } from 'nanoid'
import {
  type Strike,
  type StrikeReason,
  type StrikeStatus,
  type EscalationLevel,
  DEFAULT_GUARDRAILS_CONFIG,
} from './types.js'

// =============================================================================
// Types
// =============================================================================

export interface StrikeManagerConfig {
  /** Maximum strikes before escalation */
  maxStrikes: number
  /** Strike decay time (ms) */
  strikeDecayMs: number
  /** Logger function */
  log?: (level: string, message: string, data?: Record<string, unknown>) => void
}

export interface AddStrikeOptions {
  agentId: string
  taskId?: string
  reason: StrikeReason
  message: string
  context?: Record<string, unknown>
}

export interface RetryGuidance {
  canRetry: boolean
  message: string
  suggestions: string[]
  retryDelay?: number
}

// =============================================================================
// Strike Manager
// =============================================================================

export class StrikeManager {
  private strikes: Map<string, Strike[]> = new Map()
  private config: StrikeManagerConfig

  constructor(config?: Partial<StrikeManagerConfig>) {
    this.config = {
      maxStrikes: config?.maxStrikes ?? DEFAULT_GUARDRAILS_CONFIG.maxStrikes,
      strikeDecayMs: config?.strikeDecayMs ?? DEFAULT_GUARDRAILS_CONFIG.strikeDecayMs,
      log: config?.log,
    }
  }

  // ===========================================================================
  // Strike Operations
  // ===========================================================================

  /**
   * Add a strike for an agent
   */
  addStrike(options: AddStrikeOptions): StrikeStatus {
    const strike: Strike = {
      id: nanoid(8),
      agentId: options.agentId,
      taskId: options.taskId,
      reason: options.reason,
      message: options.message,
      timestamp: new Date(),
      context: options.context,
    }

    // Get or create strike list for agent
    const agentStrikes = this.strikes.get(options.agentId) ?? []

    // Clean expired strikes first
    const activeStrikes = this.cleanExpiredStrikes(agentStrikes)

    // Add new strike
    activeStrikes.push(strike)
    this.strikes.set(options.agentId, activeStrikes)

    // Log the strike
    if (this.config.log) {
      const level = activeStrikes.length >= this.config.maxStrikes ? 'error' : 'warn'
      this.config.log(
        level,
        `Strike ${activeStrikes.length}/${this.config.maxStrikes} for ${options.agentId}`,
        {
          reason: options.reason,
          message: options.message,
          strikeId: strike.id,
        }
      )
    }

    return this.getStatus(options.agentId)
  }

  /**
   * Clear strikes for an agent (e.g., after successful task)
   */
  clearStrikes(agentId: string): void {
    this.strikes.delete(agentId)

    if (this.config.log) {
      this.config.log('info', `Strikes cleared for ${agentId}`)
    }
  }

  /**
   * Get status for an agent
   */
  getStatus(agentId: string): StrikeStatus {
    const agentStrikes = this.strikes.get(agentId) ?? []
    const activeStrikes = this.cleanExpiredStrikes(agentStrikes)

    // Update stored strikes
    if (activeStrikes.length !== agentStrikes.length) {
      if (activeStrikes.length > 0) {
        this.strikes.set(agentId, activeStrikes)
      } else {
        this.strikes.delete(agentId)
      }
    }

    const level = this.calculateEscalationLevel(activeStrikes.length)
    const isEscalated = level === 'escalate_to_human'
    const canRetry = !isEscalated

    return {
      agentId,
      strikes: activeStrikes,
      level,
      isEscalated,
      canRetry,
      lastStrike: activeStrikes.length > 0 ? activeStrikes[activeStrikes.length - 1] : undefined,
    }
  }

  /**
   * Get all strike statuses
   */
  getAllStatuses(): StrikeStatus[] {
    const statuses: StrikeStatus[] = []

    for (const agentId of this.strikes.keys()) {
      statuses.push(this.getStatus(agentId))
    }

    return statuses
  }

  /**
   * Get retry guidance for an agent
   */
  getRetryGuidance(agentId: string): RetryGuidance {
    const status = this.getStatus(agentId)

    if (status.isEscalated) {
      return {
        canRetry: false,
        message: `Agent ${agentId} has been escalated to human review after ${this.config.maxStrikes} consecutive errors.`,
        suggestions: [
          'Review the error messages for root cause',
          'Check if the task is feasible',
          'Consider breaking the task into smaller steps',
          'Manual intervention may be required',
        ],
      }
    }

    if (status.strikes.length === 0) {
      return {
        canRetry: true,
        message: 'No strikes recorded. Agent can proceed normally.',
        suggestions: [],
      }
    }

    // Provide guidance based on strike count
    const suggestions = this.getSuggestionsForStrikes(status)

    return {
      canRetry: true,
      message: `Agent has ${status.strikes.length}/${this.config.maxStrikes} strikes. ${this.getLevelDescription(status.level)}`,
      suggestions,
      retryDelay: this.getRetryDelay(status.strikes.length),
    }
  }

  // ===========================================================================
  // Strike Helpers
  // ===========================================================================

  /**
   * Clean expired strikes
   */
  private cleanExpiredStrikes(strikes: Strike[]): Strike[] {
    const cutoff = Date.now() - this.config.strikeDecayMs
    return strikes.filter((s) => s.timestamp.getTime() > cutoff)
  }

  /**
   * Calculate escalation level from strike count
   */
  private calculateEscalationLevel(strikeCount: number): EscalationLevel {
    if (strikeCount === 0) return 'none'
    if (strikeCount >= this.config.maxStrikes) return 'escalate_to_human'
    if (strikeCount === this.config.maxStrikes - 1) return 'retry_with_guidance'
    return 'warning'
  }

  /**
   * Get level description
   */
  private getLevelDescription(level: EscalationLevel): string {
    switch (level) {
      case 'none':
        return 'Normal operation.'
      case 'warning':
        return 'First error recorded. Proceed with caution.'
      case 'retry_with_guidance':
        return 'Second error. Review approach before retrying.'
      case 'escalate_to_human':
        return 'Third error. Escalating to human review.'
    }
  }

  /**
   * Get suggestions based on strike patterns
   */
  private getSuggestionsForStrikes(status: StrikeStatus): string[] {
    const suggestions: string[] = []

    // Check for repeated reasons
    const reasonCounts = new Map<StrikeReason, number>()
    for (const strike of status.strikes) {
      reasonCounts.set(strike.reason, (reasonCounts.get(strike.reason) ?? 0) + 1)
    }

    // Add reason-specific suggestions
    if (reasonCounts.get('validation_failed')) {
      suggestions.push('Check acceptance criteria and test requirements')
      suggestions.push('Consider simpler implementation approach')
    }

    if (reasonCounts.get('task_failed')) {
      suggestions.push('Break task into smaller subtasks')
      suggestions.push('Check for missing context or dependencies')
    }

    if (reasonCounts.get('timeout')) {
      suggestions.push('Task may be too complex - consider decomposition')
      suggestions.push('Check for infinite loops or long-running operations')
    }

    if (reasonCounts.get('budget_exceeded')) {
      suggestions.push('Optimize token usage - be more concise')
      suggestions.push('Consider using smaller models for subtasks')
    }

    if (reasonCounts.get('quality_rejected')) {
      suggestions.push('Review quality requirements')
      suggestions.push('Consider more thorough planning before implementation')
    }

    // General suggestions based on count
    if (status.strikes.length >= 2) {
      suggestions.push('Consider consulting the Council for guidance')
      suggestions.push('Review previous attempt errors for patterns')
    }

    return [...new Set(suggestions)] // Dedupe
  }

  /**
   * Get retry delay based on strike count (exponential backoff)
   */
  private getRetryDelay(strikeCount: number): number {
    // 0 strikes: no delay
    // 1 strike: 5 seconds
    // 2 strikes: 15 seconds
    if (strikeCount === 0) return 0
    return Math.pow(3, strikeCount) * 1000 + 2000 // 5s, 11s, 29s
  }

  // ===========================================================================
  // Utility Methods
  // ===========================================================================

  /**
   * Get total strike count across all agents
   */
  getTotalStrikes(): number {
    let total = 0
    for (const strikes of this.strikes.values()) {
      total += this.cleanExpiredStrikes(strikes).length
    }
    return total
  }

  /**
   * Get escalated agents
   */
  getEscalatedAgents(): string[] {
    const escalated: string[] = []

    for (const agentId of this.strikes.keys()) {
      const status = this.getStatus(agentId)
      if (status.isEscalated) {
        escalated.push(agentId)
      }
    }

    return escalated
  }

  /**
   * Clear all strikes (for testing)
   */
  clearAll(): void {
    this.strikes.clear()
  }
}

// =============================================================================
// Singleton
// =============================================================================

let defaultManager: StrikeManager | null = null

/**
 * Get the default strike manager
 */
export function getStrikeManager(config?: Partial<StrikeManagerConfig>): StrikeManager {
  if (!defaultManager) {
    defaultManager = new StrikeManager(config)
  }
  return defaultManager
}

/**
 * Reset the default manager (for testing)
 */
export function resetStrikeManager(): void {
  if (defaultManager) {
    defaultManager.clearAll()
  }
  defaultManager = null
}

// =============================================================================
// Convenience Functions
// =============================================================================

/**
 * Add a strike using the default manager
 */
export function addStrike(options: AddStrikeOptions): StrikeStatus {
  return getStrikeManager().addStrike(options)
}

/**
 * Get status using the default manager
 */
export function getAgentStatus(agentId: string): StrikeStatus {
  return getStrikeManager().getStatus(agentId)
}

/**
 * Clear strikes using the default manager
 */
export function clearAgentStrikes(agentId: string): void {
  getStrikeManager().clearStrikes(agentId)
}

/**
 * Get retry guidance using the default manager
 */
export function getAgentRetryGuidance(agentId: string): RetryGuidance {
  return getStrikeManager().getRetryGuidance(agentId)
}
