/**
 * Delta9 Operator Guard
 *
 * Runtime enforcement that BLOCKS Operators from using orchestration tools.
 * Prevents infinite delegation loops and role boundary violations.
 *
 * Philosophy: "Operators execute, they don't orchestrate"
 */

import type { GuardCheckParams, GuardCheckResult } from './commander-guard.js'
import {
  formatGuardViolation as formatShared,
  createOperatorViolation,
} from '../lib/guard-formatting.js'

// =============================================================================
// Blocked Patterns
// =============================================================================

/**
 * Tools that Operators should NEVER use.
 * These are orchestration tools, not execution tools.
 */
const BLOCKED_TOOLS = [
  // Delegation tools - operators don't spawn other agents
  'delegate_task',
  'dispatch_task',
  // Mission control tools - only Commander manages missions
  'mission_create',
  'mission_add_objective',
  'mission_abort',
  'mission_complete',
  // Council tools - operators don't consult council
  'consult_council',
  'quick_consult',
  'council_convene',
]

/**
 * Agent patterns that identify Operator agents.
 * Case-insensitive matching.
 */
const OPERATOR_PATTERNS = [
  'operator',
  'operator_complex',
  'operator-complex',
  'ui_ops',
  'ui-ops',
  'scribe',
  'patcher',
]

// =============================================================================
// Guard Implementation
// =============================================================================

/**
 * Check if an agent is an Operator type.
 *
 * @param agent - Agent name to check
 * @returns True if agent is an Operator
 */
export function isOperatorAgent(agent: string): boolean {
  const lowerAgent = agent.toLowerCase()
  return OPERATOR_PATTERNS.some((pattern) => lowerAgent.includes(pattern))
}

/**
 * Check if Operator is attempting to use blocked tools.
 *
 * @param params - Guard check parameters
 * @returns Result indicating if action should be blocked
 */
export function checkOperatorGuard(params: GuardCheckParams): GuardCheckResult {
  const { agent, toolName } = params

  // Only guard Operator agents
  if (!isOperatorAgent(agent)) {
    return { blocked: false }
  }

  // Check direct tool blocking
  if (BLOCKED_TOOLS.includes(toolName)) {
    return {
      blocked: true,
      reason: `Operator cannot use ${toolName}. Operators execute, they don't orchestrate.`,
      suggestion: getSuggestion(toolName),
    }
  }

  return { blocked: false }
}

/**
 * Get suggestion message for blocked tool.
 */
function getSuggestion(toolName: string): string {
  switch (toolName) {
    case 'delegate_task':
    case 'dispatch_task':
      return 'Complete your assigned task. If you need help, report back to Commander via task_complete with a blockedReason.'

    case 'mission_create':
    case 'mission_add_objective':
    case 'mission_abort':
    case 'mission_complete':
      return 'Mission management is handled by Commander. Focus on your assigned task.'

    case 'consult_council':
    case 'quick_consult':
    case 'council_convene':
      return 'Council consultation is handled by Commander. If you need guidance, report back with task_complete.'

    default:
      return 'Focus on executing your assigned task. Report any blockers via task_complete.'
  }
}

/**
 * Format guard violation message for display to agent.
 */
export function formatOperatorViolation(result: GuardCheckResult): string {
  if (!result.blocked) {
    return ''
  }

  const ctx = createOperatorViolation(result.reason || '', result.suggestion)
  return formatShared(ctx)
}

/**
 * Get list of blocked tools for operators.
 */
export function getOperatorBlockedTools(): readonly string[] {
  return BLOCKED_TOOLS
}

/**
 * Get list of operator patterns.
 */
export function getOperatorPatterns(): readonly string[] {
  return OPERATOR_PATTERNS
}
