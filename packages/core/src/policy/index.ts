/**
 * Policy Engine - Public API
 * Priority-based tool approval system
 */

export { getPolicyEngine, PolicyEngine, resetPolicyEngine, setPolicyEngine } from './engine.js'
export {
  checkCompoundCommand,
  extractCommandName,
  matchArgs,
  matchToolName,
  stableStringify,
} from './matcher.js'
export { ApprovalMode, BUILTIN_RULES } from './rules.js'
export type {
  PolicyDecisionResult,
  PolicyDecisionType,
  PolicyEngineConfig,
  PolicyRule,
  SafetyChecker,
} from './types.js'
