/**
 * Policy Engine Types
 * Type definitions for the priority-based policy system
 */

import type { ApprovalMode } from './rules.js'

// ============================================================================
// Policy Decision
// ============================================================================

/** Decision from policy evaluation */
export type PolicyDecisionType = 'allow' | 'deny' | 'ask_user'

/** Full policy decision with metadata */
export interface PolicyDecisionResult {
  /** The decision */
  decision: PolicyDecisionType
  /** Rule that matched */
  matchedRule?: PolicyRule
  /** Human-readable reason */
  reason: string
  /** Deny message for user display */
  denyMessage?: string
}

// ============================================================================
// Policy Rule
// ============================================================================

/** A rule that determines tool approval decisions */
export interface PolicyRule {
  /** Unique rule identifier */
  name: string
  /** Tool name pattern - supports wildcards: 'bash', 'mcp__*', '*' */
  toolName?: string
  /** Regex pattern matched against stable JSON of args */
  argsPattern?: RegExp
  /** What to do when matched */
  decision: PolicyDecisionType
  /** Higher priority = checked first. Default: 0 */
  priority: number
  /** Approval modes where rule applies. Empty = all modes */
  modes?: ApprovalMode[]
  /** Allow redirections in shell commands */
  allowRedirection?: boolean
  /** Source of rule: 'builtin' | 'user' | 'project' | 'extension' */
  source: string
  /** Message shown on DENY */
  denyMessage?: string
}

// ============================================================================
// Policy Engine Config
// ============================================================================

/** Configuration for the policy engine */
export interface PolicyEngineConfig {
  /** Rules to evaluate (sorted by priority on init) */
  rules: PolicyRule[]
  /** Default decision when no rules match */
  defaultDecision: PolicyDecisionType
  /** Current approval mode */
  approvalMode: ApprovalMode
  /** Non-interactive mode (convert ASK_USER → DENY) */
  nonInteractive: boolean
}

// ============================================================================
// Safety Checker
// ============================================================================

/** Additional safety validation layer */
export interface SafetyChecker {
  /** Checker identifier */
  name: string
  /** Tool name pattern to match */
  toolName?: string
  /** Priority (higher = runs first) */
  priority: number
  /** Check function - returns override decision or null to pass through */
  check(toolName: string, args: Record<string, unknown>): Promise<PolicyDecisionResult | null>
}
