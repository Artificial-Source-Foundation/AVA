/**
 * Delta9 Commander Discipline
 *
 * Enforces the no-code rule for Commander:
 * - Blocks prohibited tools (Write, Edit, Bash)
 * - Detects code blocks in responses
 * - Logs violations for monitoring
 *
 * The Commander is a strategic planner and must NEVER write code.
 * Code writing is delegated to Operators via dispatch_task or delegate_task.
 */

import {
  CODE_PATTERNS,
  COMMANDER_ALLOWED_TOOLS,
  COMMANDER_PROHIBITED_TOOLS,
  type CommanderViolation,
} from './types.js'

// =============================================================================
// Types
// =============================================================================

export interface DisciplineCheckResult {
  allowed: boolean
  violation?: CommanderViolation
  suggestion?: string
}

export interface CommanderDisciplineConfig {
  /** Enable strict mode (block all violations) */
  strictMode: boolean
  /** Log violations */
  logViolations: boolean
  /** Custom allowed tools (extends default) */
  additionalAllowedTools?: string[]
  /** Logger function */
  log?: (level: string, message: string, data?: Record<string, unknown>) => void
}

// =============================================================================
// Violation Tracking
// =============================================================================

const violations: CommanderViolation[] = []

/**
 * Get all recorded violations
 */
export function getViolations(): CommanderViolation[] {
  return [...violations]
}

/**
 * Get recent violations (last N)
 */
export function getRecentViolations(limit: number = 10): CommanderViolation[] {
  return violations.slice(-limit)
}

/**
 * Clear violations (for testing)
 */
export function clearViolations(): void {
  violations.length = 0
}

/**
 * Record a violation
 */
function recordViolation(violation: CommanderViolation): void {
  violations.push(violation)
}

// =============================================================================
// Tool Checks
// =============================================================================

/**
 * Check if a tool is allowed for Commander
 */
export function isToolAllowed(toolName: string, config?: CommanderDisciplineConfig): boolean {
  // Check prohibited list first
  if ((COMMANDER_PROHIBITED_TOOLS as readonly string[]).includes(toolName)) {
    return false
  }

  // Check allowed list
  const allowed = new Set<string>(COMMANDER_ALLOWED_TOOLS)

  // Add any custom allowed tools
  if (config?.additionalAllowedTools) {
    for (const tool of config.additionalAllowedTools) {
      allowed.add(tool)
    }
  }

  return allowed.has(toolName)
}

/**
 * Check tool use and return result
 */
export function checkToolUse(
  toolName: string,
  agentRole: string,
  config?: CommanderDisciplineConfig
): DisciplineCheckResult {
  // Only check Commander
  if (agentRole !== 'commander') {
    return { allowed: true }
  }

  // Check if tool is prohibited
  if ((COMMANDER_PROHIBITED_TOOLS as readonly string[]).includes(toolName)) {
    const violation: CommanderViolation = {
      type: 'tool_use',
      tool: toolName,
      message: `Commander attempted to use prohibited tool: ${toolName}`,
      timestamp: new Date(),
    }

    recordViolation(violation)

    if (config?.logViolations && config.log) {
      config.log('warn', 'Commander discipline violation', {
        type: 'tool_use',
        tool: toolName,
      })
    }

    return {
      allowed: false,
      violation,
      suggestion: getSuggestionForTool(toolName),
    }
  }

  return { allowed: true }
}

/**
 * Get suggestion for how to properly delegate a tool
 */
function getSuggestionForTool(toolName: string): string {
  switch (toolName) {
    case 'Write':
    case 'Edit':
      return 'Use delegate_task or dispatch_task to have an Operator create/modify files.'
    case 'Bash':
      return 'Use delegate_task with a task description for shell operations.'
    case 'run_tests':
      return 'Use request_validation to have the Validator run tests.'
    default:
      return 'Delegate code-related tasks to appropriate Operators.'
  }
}

// =============================================================================
// Response Checks
// =============================================================================

/**
 * Check if a response contains code blocks (potential violation)
 */
export function checkResponseForCode(
  response: string,
  agentRole: string,
  config?: CommanderDisciplineConfig
): DisciplineCheckResult {
  // Only check Commander
  if (agentRole !== 'commander') {
    return { allowed: true }
  }

  for (const pattern of CODE_PATTERNS) {
    if (pattern.test(response)) {
      const violation: CommanderViolation = {
        type: 'code_block',
        pattern: pattern.toString(),
        message: 'Commander response contains code block',
        timestamp: new Date(),
      }

      recordViolation(violation)

      if (config?.logViolations && config.log) {
        config.log('warn', 'Commander discipline violation', {
          type: 'code_block',
          pattern: pattern.toString(),
        })
      }

      // In strict mode, block the response
      if (config?.strictMode) {
        return {
          allowed: false,
          violation,
          suggestion: 'Commander should describe what code is needed, not write it. Use delegate_task.',
        }
      }

      // In non-strict mode, just warn but allow
      return {
        allowed: true,
        violation,
        suggestion: 'Consider delegating code writing to an Operator.',
      }
    }
  }

  return { allowed: true }
}

// =============================================================================
// Discipline Enforcer Class
// =============================================================================

export class CommanderDisciplineEnforcer {
  private config: CommanderDisciplineConfig

  constructor(config?: Partial<CommanderDisciplineConfig>) {
    this.config = {
      strictMode: config?.strictMode ?? true,
      logViolations: config?.logViolations ?? true,
      additionalAllowedTools: config?.additionalAllowedTools,
      log: config?.log,
    }
  }

  /**
   * Check if a tool call is allowed
   */
  checkTool(toolName: string): DisciplineCheckResult {
    return checkToolUse(toolName, 'commander', this.config)
  }

  /**
   * Check if a response is allowed
   */
  checkResponse(response: string): DisciplineCheckResult {
    return checkResponseForCode(response, 'commander', this.config)
  }

  /**
   * Get allowed tools list
   */
  getAllowedTools(): string[] {
    const allowed: string[] = [...COMMANDER_ALLOWED_TOOLS]
    if (this.config.additionalAllowedTools) {
      allowed.push(...this.config.additionalAllowedTools)
    }
    return allowed
  }

  /**
   * Get prohibited tools list
   */
  getProhibitedTools(): string[] {
    return [...COMMANDER_PROHIBITED_TOOLS]
  }

  /**
   * Get violation count
   */
  getViolationCount(): number {
    return violations.length
  }

  /**
   * Get recent violations
   */
  getRecentViolations(limit: number = 10): CommanderViolation[] {
    return getRecentViolations(limit)
  }

  /**
   * Clear violations
   */
  clearViolations(): void {
    clearViolations()
  }
}

// =============================================================================
// Singleton
// =============================================================================

let defaultEnforcer: CommanderDisciplineEnforcer | null = null

/**
 * Get the default discipline enforcer
 */
export function getDisciplineEnforcer(config?: Partial<CommanderDisciplineConfig>): CommanderDisciplineEnforcer {
  if (!defaultEnforcer) {
    defaultEnforcer = new CommanderDisciplineEnforcer(config)
  }
  return defaultEnforcer
}

/**
 * Reset the default enforcer (for testing)
 */
export function resetDisciplineEnforcer(): void {
  defaultEnforcer = null
  clearViolations()
}
