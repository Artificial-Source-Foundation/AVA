/**
 * Permission System Types
 * Type definitions for user approval of destructive operations
 */

// ============================================================================
// Risk Level
// ============================================================================

/** Risk level for permission requests */
export type RiskLevel = 'low' | 'medium' | 'high' | 'critical'

/** Action type that requires permission */
export type PermissionAction = 'read' | 'write' | 'delete' | 'execute'

// ============================================================================
// Permission Request
// ============================================================================

/** A request for user permission to perform an operation */
export interface PermissionRequest {
  /** Unique identifier for this request */
  id: string
  /** Tool requesting permission */
  tool: string
  /** Type of action being performed */
  action: PermissionAction
  /** Paths affected by this operation */
  paths: string[]
  /** Human-readable reason for the operation */
  reason: string
  /** Risk level of the operation */
  risk: RiskLevel
  /** Command being executed (for execute action) */
  command?: string
  /** Timestamp when request was created */
  createdAt: number
}

// ============================================================================
// Permission Rules
// ============================================================================

/** How to handle a matching permission rule */
export type PermissionRuleAction = 'allow' | 'deny' | 'ask'

/** How long a permission decision lasts */
export type PermissionScope = 'once' | 'session' | 'persistent'

/** A rule that matches patterns and determines permission handling */
export interface PermissionRule {
  /** Unique identifier for this rule */
  id: string
  /** Glob pattern to match (file paths or commands) */
  pattern: string
  /** Action to take on match */
  action: PermissionRuleAction
  /** How long this rule applies */
  scope: PermissionScope
  /** Optional: only apply to specific tools */
  tool?: string
  /** Optional: only apply to specific action types */
  actionType?: PermissionAction
  /** Human-readable reason for this rule */
  reason?: string
  /** Priority (higher = evaluated first) */
  priority: number
  /** Whether this is a built-in rule */
  builtin?: boolean
}

// ============================================================================
// Permission Decisions
// ============================================================================

/** Result of evaluating permission rules */
export type PermissionDecision =
  | { type: 'allowed' }
  | { type: 'denied'; reason: string }
  | { type: 'ask'; request: PermissionRequest }

/** User's response to a permission request */
export interface PermissionResponse {
  /** ID of the request being responded to */
  requestId: string
  /** Whether the action is allowed */
  allowed: boolean
  /** Scope for remembering this decision */
  scope: PermissionScope
  /** Optional pattern to apply this decision to */
  pattern?: string
}

// ============================================================================
// Permission State
// ============================================================================

/** Session-specific permission grants */
export interface SessionPermissions {
  /** Patterns that have been allowed for this session */
  allowed: Set<string>
  /** Patterns that have been denied for this session */
  denied: Set<string>
}

/** Persistent permission store */
export interface PersistentPermissions {
  /** User-defined rules */
  rules: PermissionRule[]
  /** Allowed patterns */
  allowed: string[]
  /** Denied patterns */
  denied: string[]
}

// ============================================================================
// Event Types
// ============================================================================

/** Events emitted by the permission manager */
export type PermissionEvent =
  | { type: 'request'; request: PermissionRequest }
  | { type: 'granted'; requestId: string; scope: PermissionScope }
  | { type: 'denied'; requestId: string; reason: string }
  | { type: 'rule_added'; rule: PermissionRule }
  | { type: 'rule_removed'; ruleId: string }

/** Listener for permission events */
export type PermissionEventListener = (event: PermissionEvent) => void
