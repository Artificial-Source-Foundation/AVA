/**
 * Delta9 Guard Violation Formatting
 *
 * Shared formatting utilities for guard violations to ensure
 * consistent error messages across Commander and Operator guards.
 */

// =============================================================================
// Types
// =============================================================================

/**
 * Guard type identifier
 */
export type GuardType = 'COMMANDER' | 'OPERATOR'

/**
 * Context for formatting a guard violation message
 */
export interface GuardViolationContext {
  /** Type of guard that triggered the violation */
  guardType: GuardType
  /** Primary reason for the violation */
  reason: string
  /** Suggested alternative action */
  suggestion?: string
  /** Reminder about the agent's role */
  reminder: string
  /** Action the agent should take */
  action: string
}

// =============================================================================
// Formatting
// =============================================================================

/**
 * Format a guard violation message consistently
 *
 * @param ctx - Violation context with reason, suggestion, and action
 * @returns Formatted multi-line violation message
 */
export function formatGuardViolation(ctx: GuardViolationContext): string {
  const parts: string[] = [`[${ctx.guardType} GUARD VIOLATION]`, '', `Reason: ${ctx.reason}`]

  if (ctx.suggestion) {
    parts.push('', `Suggestion: ${ctx.suggestion}`)
  }

  parts.push('', `Remember: ${ctx.reminder}`, '', ctx.action)

  return parts.join('\n')
}

/**
 * Create a Commander guard violation context
 *
 * @param reason - Why the tool was blocked
 * @param suggestion - What to do instead
 * @returns Guard violation context for Commander
 */
export function createCommanderViolation(
  reason: string,
  suggestion?: string
): GuardViolationContext {
  return {
    guardType: 'COMMANDER',
    reason,
    suggestion,
    reminder: 'Commander plans and orchestrates. Operators execute.',
    action: 'Use dispatch_task() or delegate_task() to assign work.',
  }
}

/**
 * Create an Operator guard violation context
 *
 * @param reason - Why the tool was blocked
 * @param suggestion - What to do instead
 * @returns Guard violation context for Operator
 */
export function createOperatorViolation(
  reason: string,
  suggestion?: string
): GuardViolationContext {
  return {
    guardType: 'OPERATOR',
    reason,
    suggestion,
    reminder: 'Operators execute assigned tasks. Commanders orchestrate.',
    action: 'If blocked or needing scope change, use task_complete with blockedReason.',
  }
}
