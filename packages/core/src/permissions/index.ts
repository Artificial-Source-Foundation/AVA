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
export type {
  PermissionAction,
  PermissionDecision,
  PermissionEvent,
  PermissionEventListener,
  PermissionRequest,
  PermissionResponse,
  PermissionRule,
  PermissionRuleAction,
  PermissionScope,
  PersistentPermissions,
  RiskLevel,
  SessionPermissions,
} from './types.js'
