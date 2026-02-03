/**
 * Permission System
 * User approval for destructive operations
 */

export { PermissionManager } from './manager.js'
export {
  assessCommandRisk,
  assessPathRisk,
  BUILTIN_RULES,
  getHighestPathRisk,
} from './rules.js'
export {
  CorrectedError,
  type PermissionAction,
  type PermissionDecision,
  type PermissionEvent,
  type PermissionEventListener,
  type PermissionRequest,
  type PermissionResponse,
  type PermissionRule,
  type PermissionRuleAction,
  type PermissionScope,
  type PersistentPermissions,
  type RiskLevel,
  type SessionPermissions,
} from './types.js'
