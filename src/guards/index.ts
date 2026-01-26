/**
 * Delta9 Guards
 *
 * Runtime enforcement for agent role boundaries.
 */

export {
  checkCommanderGuard,
  formatGuardViolation,
  type GuardCheckParams,
  type GuardCheckResult,
} from './commander-guard.js'

export {
  checkOperatorGuard,
  formatOperatorViolation,
  isOperatorAgent,
  getOperatorBlockedTools,
  getOperatorPatterns,
} from './operator-guard.js'
