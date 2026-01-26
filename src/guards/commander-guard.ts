/**
 * Delta9 Commander Guard
 *
 * Runtime enforcement that BLOCKS Commander from doing Operator tasks.
 * Inspired by SWARM's coordinator-guard.ts pattern.
 *
 * Philosophy: "Commanders orchestrate, Operators implement"
 */

import {
  formatGuardViolation as formatShared,
  createCommanderViolation,
} from '../lib/guard-formatting.js'

// =============================================================================
// Types
// =============================================================================

export interface GuardCheckParams {
  /** Current agent name */
  agent: string
  /** Tool being invoked */
  toolName: string
  /** Tool arguments */
  toolArgs: Record<string, unknown>
}

export interface GuardCheckResult {
  /** Whether the tool call should be blocked */
  blocked: boolean
  /** Human-readable reason for blocking (shown to agent) */
  reason?: string
  /** Suggested alternative action */
  suggestion?: string
}

// =============================================================================
// Blocked Patterns
// =============================================================================

/**
 * Tools that Commander should NEVER use directly.
 * These are implementation tools, not orchestration tools.
 */
const BLOCKED_TOOLS = [
  // File modification tools (case variations for OpenCode compatibility)
  'edit',
  'Edit',
  'write',
  'Write',
  'MultiEdit',
  'file_write',
  'file_edit',
  // Notebook editing
  'NotebookEdit',
]

/**
 * Bash command patterns that Commander should not run.
 * Operators and Validator handle test execution.
 */
const BLOCKED_BASH_PATTERNS: Array<{ pattern: RegExp; reason: string; suggestion: string }> = [
  {
    pattern: /\bbun\s+test\b/i,
    reason: 'Commander cannot run bun tests directly',
    suggestion: 'Delegate to Validator using request_validation or dispatch to an Operator',
  },
  {
    pattern: /\bnpm\s+(run\s+)?test/i,
    reason: 'Commander cannot run npm tests directly',
    suggestion: 'Delegate to Validator using request_validation or dispatch to an Operator',
  },
  {
    pattern: /\bpnpm\s+(run\s+)?test/i,
    reason: 'Commander cannot run pnpm tests directly',
    suggestion: 'Delegate to Validator using request_validation or dispatch to an Operator',
  },
  {
    pattern: /\byarn\s+(run\s+)?test/i,
    reason: 'Commander cannot run yarn tests directly',
    suggestion: 'Delegate to Validator using request_validation or dispatch to an Operator',
  },
  {
    pattern: /\bvitest\b/i,
    reason: 'Commander cannot run vitest directly',
    suggestion: 'Delegate to Validator using request_validation or dispatch to an Operator',
  },
  {
    pattern: /\bjest\b/i,
    reason: 'Commander cannot run jest directly',
    suggestion: 'Delegate to Validator using request_validation or dispatch to an Operator',
  },
  {
    pattern: /\bplaywright\s+test\b/i,
    reason: 'Commander cannot run playwright tests directly',
    suggestion: 'Delegate to Validator using request_validation or dispatch to an Operator',
  },
]

// Note: We use a blocklist approach rather than allowlist.
// Any bash command not matching BLOCKED_BASH_PATTERNS is allowed.
// This gives Commander flexibility for read-only operations like:
// - ls, cat, head, tail, grep, find, wc
// - git status/log/diff/branch/show
// - npm list/outdated, etc.

// =============================================================================
// Guard Implementation
// =============================================================================

/**
 * Check if Commander is attempting to use blocked tools.
 *
 * @param params - Guard check parameters
 * @returns Result indicating if action should be blocked
 */
export function checkCommanderGuard(params: GuardCheckParams): GuardCheckResult {
  const { agent, toolName, toolArgs } = params

  // Only guard Commander agent
  if (agent.toLowerCase() !== 'commander') {
    return { blocked: false }
  }

  // Check direct tool blocking
  if (BLOCKED_TOOLS.includes(toolName)) {
    return {
      blocked: true,
      reason: `Commander cannot use ${toolName} directly. Commanders orchestrate, Operators implement.`,
      suggestion: `Use dispatch_task to delegate file modifications to an Operator, or delegate_task for background execution.`,
    }
  }

  // Check bash command patterns
  if (toolName.toLowerCase() === 'bash') {
    const command = toolArgs.command
    if (typeof command === 'string') {
      // Check against blocked patterns
      for (const { pattern, reason, suggestion } of BLOCKED_BASH_PATTERNS) {
        if (pattern.test(command)) {
          return {
            blocked: true,
            reason,
            suggestion,
          }
        }
      }
    }
  }

  return { blocked: false }
}

/**
 * Format guard violation message for display to agent.
 */
export function formatGuardViolation(result: GuardCheckResult): string {
  if (!result.blocked) {
    return ''
  }

  const ctx = createCommanderViolation(result.reason || '', result.suggestion)
  return formatShared(ctx)
}
