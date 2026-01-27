/**
 * Delta9 Compliance Hooks
 *
 * Workflow compliance reminders that inject hints after tool execution.
 * Reminds agents to follow proper workflow (Commander delegates, Operators validate).
 *
 * Pattern from: froggy phase reminders
 *
 * Compliance Rules:
 * - Commander should NOT read/modify code directly → delegate to Operators
 * - Operators should validate after completing tasks
 * - Validators should report clear pass/fail results
 */

import { getNamedLogger } from './logger.js'

const log = getNamedLogger('compliance-hooks')

// =============================================================================
// Types
// =============================================================================

/**
 * Agent role for compliance checking
 */
export type AgentRole = 'commander' | 'operator' | 'validator' | 'support' | 'unknown'

/**
 * Tool execution context
 */
export interface ToolContext {
  /** Tool name that was executed */
  toolName: string
  /** Session ID */
  sessionId: string
  /** Agent role (if known) */
  role?: AgentRole
  /** Tool execution result */
  result?: 'success' | 'failure' | 'unknown'
  /** Recent tool history (for pattern detection) */
  recentTools?: string[]
}

/**
 * Compliance check result
 */
export interface ComplianceCheckResult {
  /** Whether any violations were detected */
  hasViolation: boolean
  /** Severity level */
  severity: 'info' | 'warning' | 'error'
  /** Reminder message (if any) */
  reminder: string | null
  /** Violated rule name */
  rule?: string
  /** Suggestion for correction */
  suggestion?: string
}

/**
 * Compliance rule definition
 */
export interface ComplianceRule {
  /** Rule name */
  name: string
  /** Rule description */
  description: string
  /** Applicable agent roles */
  roles: AgentRole[]
  /** Check function */
  check: (context: ToolContext) => ComplianceCheckResult | null
  /** Whether rule is enabled */
  enabled: boolean
  /** Severity level */
  severity: 'info' | 'warning' | 'error'
}

// =============================================================================
// Compliance Rules Registry
// =============================================================================

const rules: Map<string, ComplianceRule> = new Map()

/**
 * Register a compliance rule
 */
export function registerRule(rule: ComplianceRule): void {
  rules.set(rule.name, rule)
  log.debug(`Registered compliance rule: ${rule.name}`)
}

/**
 * Unregister a compliance rule
 */
export function unregisterRule(name: string): boolean {
  return rules.delete(name)
}

/**
 * Enable a rule
 */
export function enableRule(name: string): void {
  const rule = rules.get(name)
  if (rule) rule.enabled = true
}

/**
 * Disable a rule
 */
export function disableRule(name: string): void {
  const rule = rules.get(name)
  if (rule) rule.enabled = false
}

/**
 * Get all registered rules
 */
export function getRules(): ComplianceRule[] {
  return Array.from(rules.values())
}

/**
 * Clear all rules (for testing)
 */
export function clearRules(): void {
  rules.clear()
}

// =============================================================================
// Compliance Checking
// =============================================================================

/**
 * Check compliance for a tool execution
 */
export function checkCompliance(context: ToolContext): ComplianceCheckResult {
  const role = context.role ?? 'unknown'

  for (const rule of rules.values()) {
    // Skip disabled rules
    if (!rule.enabled) continue

    // Skip if role doesn't match
    if (!rule.roles.includes(role) && !rule.roles.includes('unknown')) continue

    // Run the check
    const result = rule.check(context)
    if (result && result.hasViolation) {
      log.debug(`Compliance violation: ${rule.name}`, { context })
      return result
    }
  }

  return {
    hasViolation: false,
    severity: 'info',
    reminder: null,
  }
}

/**
 * Get compliance reminder for a tool execution
 *
 * Convenience function that returns just the reminder string.
 */
export function getComplianceReminder(context: ToolContext): string | null {
  const result = checkCompliance(context)
  return result.reminder
}

// =============================================================================
// Tool Categories
// =============================================================================

/**
 * Tools that read code (Commander should delegate instead)
 */
const CODE_READ_TOOLS = [
  'read_file',
  'grep',
  'glob',
  'list_files',
  'search_code',
  'view_file',
]

/**
 * Tools that modify code (Commander should delegate instead)
 */
const CODE_MODIFY_TOOLS = [
  'write_file',
  'edit_file',
  'replace_in_file',
  'create_file',
  'delete_file',
  'apply_patch',
]

/**
 * Delegation tools
 */
const DELEGATION_TOOLS = [
  'dispatch_task',
  'delegate_task',
  'spawn_operator',
  'launch_squadron',
]

/**
 * Validation tools
 */
const VALIDATION_TOOLS = [
  'validation_result',
  'report_validation',
  'task_validate',
]

/**
 * Task completion tools
 */
const TASK_COMPLETION_TOOLS = [
  'task_complete',
  'complete_task',
  'task_done',
  'report_completion',
]

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * Check if tool is a code reading tool
 */
export function isCodeReadTool(toolName: string): boolean {
  return CODE_READ_TOOLS.includes(toolName.toLowerCase())
}

/**
 * Check if tool is a code modifying tool
 */
export function isCodeModifyTool(toolName: string): boolean {
  return CODE_MODIFY_TOOLS.includes(toolName.toLowerCase())
}

/**
 * Check if tool is a delegation tool
 */
export function isDelegationTool(toolName: string): boolean {
  return DELEGATION_TOOLS.includes(toolName.toLowerCase())
}

/**
 * Check if tool is a validation tool
 */
export function isValidationTool(toolName: string): boolean {
  return VALIDATION_TOOLS.includes(toolName.toLowerCase())
}

/**
 * Check if tool is a task completion tool
 */
export function isTaskCompletionTool(toolName: string): boolean {
  return TASK_COMPLETION_TOOLS.includes(toolName.toLowerCase())
}

/**
 * Check if recent tools include any from a category
 */
function hasRecentTool(recentTools: string[] | undefined, checker: (tool: string) => boolean): boolean {
  if (!recentTools) return false
  return recentTools.some(checker)
}

// =============================================================================
// Default Rules
// =============================================================================

/**
 * Rule: Commander should not read code directly
 */
const COMMANDER_NO_CODE_READ: ComplianceRule = {
  name: 'commander-no-code-read',
  description: 'Commander should delegate code reading to Operators',
  roles: ['commander'],
  severity: 'warning',
  enabled: true,
  check: (context) => {
    if (isCodeReadTool(context.toolName)) {
      return {
        hasViolation: true,
        severity: 'warning',
        reminder: '⚠️ Commander reads but does not implement. Consider delegating code analysis to an Operator.',
        rule: 'commander-no-code-read',
        suggestion: 'Use dispatch_task to delegate code analysis',
      }
    }
    return null
  },
}

/**
 * Rule: Commander should not modify code directly
 */
const COMMANDER_NO_CODE_MODIFY: ComplianceRule = {
  name: 'commander-no-code-modify',
  description: 'Commander should never modify code directly',
  roles: ['commander'],
  severity: 'error',
  enabled: true,
  check: (context) => {
    if (isCodeModifyTool(context.toolName)) {
      return {
        hasViolation: true,
        severity: 'error',
        reminder: '🚫 Commander NEVER writes code. Dispatch to an Operator immediately.',
        rule: 'commander-no-code-modify',
        suggestion: 'Use dispatch_task to delegate code changes to an Operator',
      }
    }
    return null
  },
}

/**
 * Rule: Operator should validate after task completion
 */
const OPERATOR_VALIDATE_AFTER_COMPLETE: ComplianceRule = {
  name: 'operator-validate-after-complete',
  description: 'Operator should run validation after completing a task',
  roles: ['operator'],
  severity: 'info',
  enabled: true,
  check: (context) => {
    // Check if current tool is task completion
    if (isTaskCompletionTool(context.toolName)) {
      // Check if validation was run recently
      if (!hasRecentTool(context.recentTools, isValidationTool)) {
        return {
          hasViolation: true,
          severity: 'info',
          reminder: '💡 Task marked complete but not validated. Consider running validation_result.',
          rule: 'operator-validate-after-complete',
          suggestion: 'Run validation_result before marking task complete',
        }
      }
    }
    return null
  },
}

/**
 * Rule: Commander should delegate after reading
 */
const COMMANDER_DELEGATE_AFTER_READ: ComplianceRule = {
  name: 'commander-delegate-after-read',
  description: 'Commander should delegate after analyzing code',
  roles: ['commander'],
  severity: 'info',
  enabled: true,
  check: (context) => {
    // This rule checks accumulated behavior
    // If Commander has read code but not delegated, remind them
    if (context.recentTools && context.recentTools.length >= 3) {
      const hasReads = context.recentTools.some(isCodeReadTool)
      const hasDelegation = context.recentTools.some(isDelegationTool)

      if (hasReads && !hasDelegation) {
        // Only trigger on non-read tool to avoid spamming
        if (!isCodeReadTool(context.toolName) && !isDelegationTool(context.toolName)) {
          return {
            hasViolation: true,
            severity: 'info',
            reminder: '💡 Commander has been reading code. Ready to delegate to an Operator?',
            rule: 'commander-delegate-after-read',
            suggestion: 'Use dispatch_task to delegate implementation',
          }
        }
      }
    }
    return null
  },
}

/**
 * Exploration tools (should be delegated to RECON/scout)
 */
const EXPLORATION_TOOLS = ['glob', 'list_files', 'search_code']

/**
 * Check if tool is an exploration tool
 */
function isExplorationTool(toolName: string): boolean {
  return EXPLORATION_TOOLS.includes(toolName.toLowerCase())
}

/**
 * Rule: Commander should delegate bulk exploration to RECON (BUG-10 fix)
 *
 * Triggers when Commander uses multiple Glob/Read calls instead of
 * delegating reconnaissance to the RECON/scout agent.
 */
const COMMANDER_DELEGATE_RECON: ComplianceRule = {
  name: 'commander-delegate-recon',
  description: 'Commander should delegate codebase exploration to RECON agent',
  roles: ['commander'],
  severity: 'warning',
  enabled: true,
  check: (context) => {
    // Check for exploration patterns
    if (context.recentTools && context.recentTools.length >= 2) {
      const explorationCount = context.recentTools.filter(isExplorationTool).length
      const readCount = context.recentTools.filter((t) => t.toLowerCase() === 'read_file').length

      // If Commander has done 2+ exploration tools OR 3+ file reads, suggest RECON
      if (explorationCount >= 2 || readCount >= 3) {
        const hasDelegation = context.recentTools.some(isDelegationTool)

        if (!hasDelegation && (isExplorationTool(context.toolName) || isCodeReadTool(context.toolName))) {
          return {
            hasViolation: true,
            severity: 'warning',
            reminder:
              '🔍 Commander is doing reconnaissance. Delegate exploration to RECON (scout) agent for efficient codebase scanning.',
            rule: 'commander-delegate-recon',
            suggestion: 'Use delegate_task(agent="scout", prompt="Explore...") for codebase reconnaissance',
          }
        }
      }
    }
    return null
  },
}

/**
 * Register all default rules
 */
export function registerDefaultRules(): void {
  // Order matters! More specific rules first, general rules last.
  // Pattern-based rules (detect multi-tool patterns) take precedence over single-tool rules.
  registerRule(COMMANDER_DELEGATE_RECON) // BUG-10: Pattern detection (2+ exploration or 3+ reads)
  registerRule(COMMANDER_NO_CODE_READ) // General: Any single code read
  registerRule(COMMANDER_NO_CODE_MODIFY) // Critical: Never modify code
  registerRule(OPERATOR_VALIDATE_AFTER_COMPLETE)
  registerRule(COMMANDER_DELEGATE_AFTER_READ)
  log.info('Registered default compliance rules')
}

// =============================================================================
// Tool History Tracking
// =============================================================================

/**
 * Track recent tools per session
 */
const sessionToolHistory: Map<string, string[]> = new Map()
const MAX_TOOL_HISTORY = 10

/**
 * Track a tool execution
 */
export function trackToolExecution(sessionId: string, toolName: string): void {
  let history = sessionToolHistory.get(sessionId)
  if (!history) {
    history = []
    sessionToolHistory.set(sessionId, history)
  }

  history.push(toolName)

  // Keep only last N tools
  if (history.length > MAX_TOOL_HISTORY) {
    history.shift()
  }
}

/**
 * Get recent tools for a session
 */
export function getRecentTools(sessionId: string): string[] {
  return sessionToolHistory.get(sessionId) ?? []
}

/**
 * Clear tool history for a session
 */
export function clearToolHistory(sessionId: string): void {
  sessionToolHistory.delete(sessionId)
}

/**
 * Clear all tool history (for testing)
 */
export function clearAllToolHistory(): void {
  sessionToolHistory.clear()
}

// =============================================================================
// Integration Helper
// =============================================================================

/**
 * Create a compliance context from session state
 */
export function createComplianceContext(
  sessionId: string,
  toolName: string,
  role?: AgentRole
): ToolContext {
  return {
    toolName,
    sessionId,
    role,
    recentTools: getRecentTools(sessionId),
  }
}

/**
 * Full compliance check with tracking
 *
 * Use this as the main entry point for compliance checking.
 */
export function checkAndTrack(
  sessionId: string,
  toolName: string,
  role?: AgentRole
): ComplianceCheckResult {
  // Track the tool execution
  trackToolExecution(sessionId, toolName)

  // Create context and check compliance
  const context = createComplianceContext(sessionId, toolName, role)
  return checkCompliance(context)
}
