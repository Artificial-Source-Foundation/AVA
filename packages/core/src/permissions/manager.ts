/**
 * Permission Manager
 * Handles permission requests, rule matching, and user approval flow
 */

import { assessCommandRisk, BUILTIN_RULES, getHighestPathRisk } from './rules.js'
import type {
  PermissionAction,
  PermissionDecision,
  PermissionEvent,
  PermissionEventListener,
  PermissionRequest,
  PermissionResponse,
  PermissionRule,
  RiskLevel,
  SessionPermissions,
} from './types.js'

// ============================================================================
// Utilities
// ============================================================================

/** Generate a unique ID */
function generateId(): string {
  return `perm_${Date.now()}_${Math.random().toString(36).substring(2, 9)}`
}

/**
 * Match a value against a glob-like pattern
 * Supports * (any characters) and ** (any path segments)
 */
function matchPattern(value: string, pattern: string): boolean {
  // Escape regex special chars except * and **
  let regexPattern = pattern
    .replace(/[.+?^${}()|[\]\\]/g, '\\$&')
    .replace(/\*\*/g, '{{GLOBSTAR}}')
    .replace(/\*/g, '[^/]*')
    .replace(/{{GLOBSTAR}}/g, '.*')

  // Anchor the pattern
  regexPattern = `^${regexPattern}$`

  try {
    return new RegExp(regexPattern).test(value)
  } catch {
    return false
  }
}

// ============================================================================
// Permission Manager
// ============================================================================

export class PermissionManager {
  private sessionPermissions: SessionPermissions = {
    allowed: new Set(),
    denied: new Set(),
  }

  private userRules: PermissionRule[] = []
  private pendingRequests = new Map<string, PermissionRequest>()
  private listeners = new Set<PermissionEventListener>()

  constructor(userRules?: PermissionRule[]) {
    if (userRules) {
      this.userRules = userRules
    }
  }

  // --------------------------------------------------------------------------
  // Event Handling
  // --------------------------------------------------------------------------

  /** Subscribe to permission events */
  on(listener: PermissionEventListener): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  /** Emit an event to all listeners */
  private emit(event: PermissionEvent): void {
    for (const listener of this.listeners) {
      try {
        listener(event)
      } catch {
        // Ignore listener errors
      }
    }
  }

  // --------------------------------------------------------------------------
  // Rule Management
  // --------------------------------------------------------------------------

  /** Get all rules (built-in + user) sorted by priority */
  getAllRules(): PermissionRule[] {
    return [...BUILTIN_RULES, ...this.userRules].sort((a, b) => b.priority - a.priority)
  }

  /** Add a user-defined rule */
  addRule(rule: Omit<PermissionRule, 'id' | 'builtin'>): PermissionRule {
    const newRule: PermissionRule = {
      ...rule,
      id: generateId(),
      builtin: false,
    }
    this.userRules.push(newRule)
    this.emit({ type: 'rule_added', rule: newRule })
    return newRule
  }

  /** Remove a user-defined rule by ID */
  removeRule(ruleId: string): boolean {
    const index = this.userRules.findIndex((r) => r.id === ruleId)
    if (index === -1) return false

    this.userRules.splice(index, 1)
    this.emit({ type: 'rule_removed', ruleId })
    return true
  }

  /** Find a rule matching the given criteria */
  private findMatchingRule(
    value: string,
    tool?: string,
    actionType?: PermissionAction
  ): PermissionRule | undefined {
    const rules = this.getAllRules()

    for (const rule of rules) {
      // Check tool filter
      if (rule.tool && rule.tool !== tool) continue

      // Check action type filter
      if (rule.actionType && rule.actionType !== actionType) continue

      // Check pattern match
      if (matchPattern(value, rule.pattern)) {
        return rule
      }
    }

    return undefined
  }

  // --------------------------------------------------------------------------
  // Session Permissions
  // --------------------------------------------------------------------------

  /** Clear session permissions (on new session) */
  clearSession(): void {
    this.sessionPermissions = {
      allowed: new Set(),
      denied: new Set(),
    }
    this.pendingRequests.clear()
  }

  /** Check if a pattern is allowed in the current session */
  private isSessionAllowed(pattern: string): boolean {
    return this.sessionPermissions.allowed.has(pattern)
  }

  /** Check if a pattern is denied in the current session */
  private isSessionDenied(pattern: string): boolean {
    return this.sessionPermissions.denied.has(pattern)
  }

  /** Grant session permission for a pattern */
  private grantSessionPermission(pattern: string): void {
    this.sessionPermissions.allowed.add(pattern)
    this.sessionPermissions.denied.delete(pattern)
  }

  /** Deny session permission for a pattern */
  private denySessionPermission(pattern: string): void {
    this.sessionPermissions.denied.add(pattern)
    this.sessionPermissions.allowed.delete(pattern)
  }

  // --------------------------------------------------------------------------
  // Permission Checking
  // --------------------------------------------------------------------------

  /**
   * Check permission for a file path operation
   */
  checkPath(
    tool: string,
    action: PermissionAction,
    paths: string[],
    reason: string
  ): PermissionDecision {
    // Check each path against rules
    for (const path of paths) {
      // Check session permissions first
      if (this.isSessionDenied(path)) {
        return { type: 'denied', reason: 'Previously denied in this session' }
      }

      // Find matching rule
      const rule = this.findMatchingRule(path, tool, action)
      if (rule) {
        if (rule.action === 'deny') {
          return { type: 'denied', reason: rule.reason ?? 'Denied by rule' }
        }
        if (rule.action === 'ask' && !this.isSessionAllowed(path)) {
          // Need to ask user
          const risk = getHighestPathRisk(paths)
          const request = this.createRequest(tool, action, paths, reason, risk.risk)
          return { type: 'ask', request }
        }
      }
    }

    return { type: 'allowed' }
  }

  /**
   * Check permission for a shell command
   */
  checkCommand(command: string, reason: string): PermissionDecision {
    // Check session permissions
    if (this.isSessionDenied(command)) {
      return { type: 'denied', reason: 'Previously denied in this session' }
    }

    // Find matching rule
    const rule = this.findMatchingRule(command, 'bash', 'execute')
    if (rule) {
      if (rule.action === 'deny') {
        return { type: 'denied', reason: rule.reason ?? 'Denied by rule' }
      }
      if (rule.action === 'ask' && !this.isSessionAllowed(command)) {
        const risk = assessCommandRisk(command)
        const request = this.createRequest('bash', 'execute', [], reason, risk.risk, command)
        return { type: 'ask', request }
      }
    }

    // Check for inherently risky commands
    const risk = assessCommandRisk(command)
    if ((risk.risk === 'high' || risk.risk === 'critical') && !this.isSessionAllowed(command)) {
      const request = this.createRequest('bash', 'execute', [], reason, risk.risk, command)
      return { type: 'ask', request }
    }

    return { type: 'allowed' }
  }

  /**
   * Create a permission request
   */
  private createRequest(
    tool: string,
    action: PermissionAction,
    paths: string[],
    reason: string,
    risk: RiskLevel,
    command?: string
  ): PermissionRequest {
    const request: PermissionRequest = {
      id: generateId(),
      tool,
      action,
      paths,
      reason,
      risk,
      command,
      createdAt: Date.now(),
    }

    this.pendingRequests.set(request.id, request)
    this.emit({ type: 'request', request })

    return request
  }

  // --------------------------------------------------------------------------
  // Response Handling
  // --------------------------------------------------------------------------

  /**
   * Handle user response to a permission request
   */
  handleResponse(response: PermissionResponse): boolean {
    const request = this.pendingRequests.get(response.requestId)
    if (!request) {
      return false
    }

    this.pendingRequests.delete(response.requestId)

    // Determine the pattern to remember
    const pattern = response.pattern ?? request.command ?? request.paths[0] ?? request.tool

    if (response.allowed) {
      // Grant permission
      if (response.scope === 'session' || response.scope === 'once') {
        this.grantSessionPermission(pattern)
      }
      if (response.scope === 'persistent') {
        this.addRule({
          pattern,
          action: 'allow',
          scope: 'persistent',
          tool: request.tool,
          actionType: request.action,
          reason: `Allowed by user`,
          priority: 500,
        })
      }
      this.emit({ type: 'granted', requestId: request.id, scope: response.scope })
    } else {
      // Deny permission
      if (response.scope === 'session' || response.scope === 'once') {
        this.denySessionPermission(pattern)
      }
      if (response.scope === 'persistent') {
        this.addRule({
          pattern,
          action: 'deny',
          scope: 'persistent',
          tool: request.tool,
          actionType: request.action,
          reason: `Denied by user`,
          priority: 500,
        })
      }
      this.emit({ type: 'denied', requestId: request.id, reason: 'Denied by user' })
    }

    return true
  }

  /**
   * Get a pending request by ID
   */
  getPendingRequest(requestId: string): PermissionRequest | undefined {
    return this.pendingRequests.get(requestId)
  }

  /**
   * Get all pending requests
   */
  getPendingRequests(): PermissionRequest[] {
    return Array.from(this.pendingRequests.values())
  }

  /**
   * Cancel a pending request
   */
  cancelRequest(requestId: string): boolean {
    return this.pendingRequests.delete(requestId)
  }

  // --------------------------------------------------------------------------
  // Serialization
  // --------------------------------------------------------------------------

  /** Export user rules for persistence */
  exportRules(): PermissionRule[] {
    return [...this.userRules]
  }

  /** Import user rules from persistence */
  importRules(rules: PermissionRule[]): void {
    this.userRules = rules.filter((r) => !r.builtin)
  }

  /** Export session state */
  exportSession(): { allowed: string[]; denied: string[] } {
    return {
      allowed: Array.from(this.sessionPermissions.allowed),
      denied: Array.from(this.sessionPermissions.denied),
    }
  }

  /** Import session state */
  importSession(state: { allowed: string[]; denied: string[] }): void {
    this.sessionPermissions = {
      allowed: new Set(state.allowed),
      denied: new Set(state.denied),
    }
  }
}
