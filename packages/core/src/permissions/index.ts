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
// Command validation (Sprint 1: Security)
export {
  type CommandPermissionConfig,
  type CommandValidationReason,
  type CommandValidationResult,
  CommandValidator,
  DEV_WORKFLOW_CONFIG,
  getCommandValidator,
  quickDangerCheck,
  READ_ONLY_CONFIG,
  STRICT_CONFIG,
  setCommandPermissions,
  validateCommand,
} from './command-validator.js'
export { PermissionManager } from './manager.js'
// Persistent approvals (cross-session "always allow")
export {
  addPersistentApproval,
  checkApprovalWithPersistence,
  clearAllApprovals,
  clearSessionApprovals,
  getAllApprovals,
  getToolApprovals,
  hasPersistentApproval,
  loadPersistentApprovals,
  type PersistentApproval,
  type PersistentApprovalsData,
  removePersistentApproval,
  savePersistentApprovals,
} from './persistent-approvals.js'
// Quote-aware shell parsing
export {
  COMMAND_SEPARATORS,
  type CommandSegment,
  createQuoteState,
  type DangerousCharResult,
  detectDangerousCharacters,
  detectRedirects,
  extractSubshells,
  isInSafeContext,
  isInsideQuotes,
  parseCommandSegments,
  processChar,
  type QuoteState,
  REDIRECT_OPERATORS,
  UNICODE_SEPARATORS,
} from './quote-parser.js'
export {
  assessCommandRisk,
  assessPathRisk,
  BUILTIN_RULES,
  getHighestPathRisk,
} from './rules.js'
// Trusted Folders
export {
  getTrustedFolderManager,
  resetTrustedFolderManager,
  setTrustedFolderManager,
  type TrustCheckResult,
  type TrustedFolder,
  TrustedFolderManager,
} from './trusted-folders.js'
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
