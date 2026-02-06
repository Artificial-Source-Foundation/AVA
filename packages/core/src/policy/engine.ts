/**
 * Policy Engine
 * Priority-based rule evaluation for tool approval decisions.
 *
 * Features:
 * - Priority-sorted rules (higher priority = checked first)
 * - Wildcard tool name patterns (*, mcp__*, delegate_*)
 * - Regex args matching on stable JSON
 * - Approval mode scoping (default, yolo, plan, auto_edit)
 * - Compound shell command recursive validation
 * - Safety checker integration layer
 * - Non-interactive mode (ASK_USER → DENY)
 *
 * Decision flow:
 * 1. Sort rules by priority (descending)
 * 2. For each rule: check tool name, args, mode
 * 3. First match wins
 * 4. Safety checkers run after (can override ALLOW → ASK_USER/DENY)
 * 5. Non-interactive converts ASK_USER → DENY
 */

import { checkCompoundCommand, extractCommandName, matchArgs, matchToolName } from './matcher.js'
import { ApprovalMode, BUILTIN_RULES } from './rules.js'
import type {
  PolicyDecisionResult,
  PolicyEngineConfig,
  PolicyRule,
  SafetyChecker,
} from './types.js'

// ============================================================================
// Policy Engine
// ============================================================================

export class PolicyEngine {
  private rules: PolicyRule[]
  private safetyCheckers: SafetyChecker[]
  private config: PolicyEngineConfig

  constructor(config?: Partial<PolicyEngineConfig>) {
    this.config = {
      rules: config?.rules ?? [...BUILTIN_RULES],
      defaultDecision: config?.defaultDecision ?? 'ask_user',
      approvalMode: config?.approvalMode ?? ApprovalMode.DEFAULT,
      nonInteractive: config?.nonInteractive ?? false,
    }

    // Sort rules by priority (descending)
    this.rules = [...this.config.rules].sort((a, b) => b.priority - a.priority)
    this.safetyCheckers = []
  }

  // ==========================================================================
  // Core Evaluation
  // ==========================================================================

  /**
   * Check a tool call against policy rules.
   *
   * @param toolName - Name of the tool being called
   * @param args - Tool arguments
   * @returns Policy decision with metadata
   */
  async check(toolName: string, args: Record<string, unknown>): Promise<PolicyDecisionResult> {
    // Special handling for bash commands
    if (toolName === 'bash' && typeof args.command === 'string') {
      return this.checkBashCommand(args.command, args)
    }

    // Standard rule evaluation
    let decision = this.evaluateRules(toolName, args)

    // Run safety checkers if not already DENY
    if (decision.decision !== 'deny') {
      const checkerResult = await this.runSafetyCheckers(toolName, args)
      if (checkerResult) {
        decision = checkerResult
      }
    }

    // Non-interactive mode: convert ASK_USER → DENY
    if (this.config.nonInteractive && decision.decision === 'ask_user') {
      return {
        decision: 'deny',
        matchedRule: decision.matchedRule,
        reason: `Non-interactive mode: ${decision.reason}`,
        denyMessage: 'Operation requires user approval but running in non-interactive mode.',
      }
    }

    return decision
  }

  /**
   * Evaluate rules against a tool call.
   * First matching rule wins (rules are sorted by priority).
   */
  private evaluateRules(toolName: string, args: Record<string, unknown>): PolicyDecisionResult {
    for (const rule of this.rules) {
      if (this.ruleMatches(rule, toolName, args)) {
        return {
          decision: rule.decision,
          matchedRule: rule,
          reason: `Matched rule: ${rule.name} (priority ${rule.priority})`,
          denyMessage: rule.denyMessage,
        }
      }
    }

    // No rule matched - use default
    return {
      decision: this.config.defaultDecision,
      reason: 'No matching rule found, using default decision',
    }
  }

  /**
   * Check if a rule matches the current context.
   */
  private ruleMatches(rule: PolicyRule, toolName: string, args: Record<string, unknown>): boolean {
    // Check approval mode
    if (rule.modes && rule.modes.length > 0) {
      if (!rule.modes.includes(this.config.approvalMode)) {
        return false
      }
    }

    // Check tool name pattern
    if (rule.toolName) {
      if (!matchToolName(rule.toolName, toolName)) {
        return false
      }
    }

    // Check args pattern
    if (rule.argsPattern) {
      if (!matchArgs(rule.argsPattern, args)) {
        return false
      }
    }

    return true
  }

  // ==========================================================================
  // Bash Command Handling
  // ==========================================================================

  /**
   * Special handling for bash commands:
   * - Split compound commands (&&, ||, |, ;)
   * - Check each sub-command independently
   * - Aggregate decisions pessimistically
   * - Downgrade for redirections
   */
  private async checkBashCommand(
    command: string,
    fullArgs: Record<string, unknown>
  ): Promise<PolicyDecisionResult> {
    // Check the compound command structure
    const aggregated = checkCompoundCommand(command, (subcommand: string) => {
      const cmdName = extractCommandName(subcommand)
      // Evaluate against bash-specific args
      const subArgs = { ...fullArgs, command: subcommand, baseCommand: cmdName }
      const result = this.evaluateRules('bash', subArgs)
      return result.decision
    })

    // Build result
    const baseResult = this.evaluateRules('bash', fullArgs)

    // If compound check found worse decision, use it
    if (aggregated === 'deny') {
      return {
        decision: 'deny',
        matchedRule: baseResult.matchedRule,
        reason: 'Compound command contains denied sub-command',
      }
    }

    if (aggregated === 'ask_user' && baseResult.decision === 'allow') {
      return {
        decision: 'ask_user',
        matchedRule: baseResult.matchedRule,
        reason: 'Compound command contains sub-command requiring approval',
      }
    }

    // Run safety checkers
    if (baseResult.decision !== 'deny') {
      const checkerResult = await this.runSafetyCheckers('bash', fullArgs)
      if (checkerResult) {
        return checkerResult
      }
    }

    // Non-interactive handling
    if (this.config.nonInteractive && baseResult.decision === 'ask_user') {
      return {
        decision: 'deny',
        matchedRule: baseResult.matchedRule,
        reason: `Non-interactive mode: ${baseResult.reason}`,
        denyMessage: 'Command requires user approval but running in non-interactive mode.',
      }
    }

    return baseResult
  }

  // ==========================================================================
  // Safety Checkers
  // ==========================================================================

  /**
   * Run safety checkers in priority order.
   * Returns first non-null result (override decision).
   */
  private async runSafetyCheckers(
    toolName: string,
    args: Record<string, unknown>
  ): Promise<PolicyDecisionResult | null> {
    const matching = this.safetyCheckers
      .filter((c) => !c.toolName || matchToolName(c.toolName, toolName))
      .sort((a, b) => b.priority - a.priority)

    for (const checker of matching) {
      const result = await checker.check(toolName, args)
      if (result) return result
    }

    return null
  }

  // ==========================================================================
  // Rule Management
  // ==========================================================================

  /**
   * Add a rule and re-sort.
   */
  addRule(rule: PolicyRule): void {
    this.rules.push(rule)
    this.rules.sort((a, b) => b.priority - a.priority)
  }

  /**
   * Remove a rule by name.
   */
  removeRule(name: string): boolean {
    const idx = this.rules.findIndex((r) => r.name === name)
    if (idx === -1) return false
    this.rules.splice(idx, 1)
    return true
  }

  /**
   * Get all rules (sorted by priority).
   */
  getRules(): readonly PolicyRule[] {
    return this.rules
  }

  /**
   * Add a safety checker.
   */
  addSafetyChecker(checker: SafetyChecker): void {
    this.safetyCheckers.push(checker)
  }

  // ==========================================================================
  // Mode Management
  // ==========================================================================

  /**
   * Set the current approval mode.
   */
  setApprovalMode(mode: ApprovalMode): void {
    this.config.approvalMode = mode
  }

  /**
   * Get the current approval mode.
   */
  getApprovalMode(): ApprovalMode {
    return this.config.approvalMode
  }

  /**
   * Set non-interactive mode.
   */
  setNonInteractive(value: boolean): void {
    this.config.nonInteractive = value
  }
}

// ============================================================================
// Singleton
// ============================================================================

let defaultEngine: PolicyEngine | null = null

/**
 * Get the default policy engine instance.
 */
export function getPolicyEngine(): PolicyEngine {
  if (!defaultEngine) {
    defaultEngine = new PolicyEngine()
  }
  return defaultEngine
}

/**
 * Set the default policy engine (for testing or custom configuration).
 */
export function setPolicyEngine(engine: PolicyEngine): void {
  defaultEngine = engine
}

/**
 * Reset the default policy engine.
 */
export function resetPolicyEngine(): void {
  defaultEngine = null
}
