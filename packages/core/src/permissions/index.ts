/**
 * Permission System
 * User approval for destructive operations
 */

// Auto-approval system
export {
  AUTO_APPROVE_SAFE_COMMANDS,
  type AutoApprovalActions,
  type AutoApprovalResult,
  type AutoApprovalSettings,
  checkBrowserAutoApproval,
  checkCommandAutoApproval,
  checkFileAutoApproval,
  checkMcpAutoApproval,
  checkWebFetchAutoApproval,
  DEFAULT_AUTO_APPROVAL_SETTINGS,
  disableYoloMode,
  enableYoloMode,
  extractBaseCommand,
  getAutoApprovalSettings,
  isCommandSafe,
  isPathBlocked,
  isPathLocal,
  isPathTrusted,
  resetAutoApprovalSettings,
  setAutoApprovalSettings,
  shouldAutoApprove,
  YOLO_AUTO_APPROVAL_SETTINGS,
} from './auto-approve.js'
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
