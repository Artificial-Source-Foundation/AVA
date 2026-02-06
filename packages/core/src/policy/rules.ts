/**
 * Built-in Policy Rules
 * Priority-based rules for tool approval decisions
 */

import type { PolicyRule } from './types.js'

// ============================================================================
// Approval Modes
// ============================================================================

export enum ApprovalMode {
  /** Default mode - standard approval rules apply */
  DEFAULT = 'default',
  /** Auto-edit mode - writes approved, destructive operations ask */
  AUTO_EDIT = 'auto_edit',
  /** YOLO mode - everything approved (except critical blocks) */
  YOLO = 'yolo',
  /** Plan mode - read-only operations only */
  PLAN = 'plan',
}

// ============================================================================
// Built-in Rules
// ============================================================================

/**
 * Built-in policy rules sorted by priority (descending).
 *
 * Priority ranges:
 * - 1000+: Mode-specific overrides (plan mode, yolo mode)
 * - 500-999: Critical safety rules (blocked patterns, dangerous commands)
 * - 100-499: Default tool-specific rules
 * - 0-99: Fallback rules
 */
export const BUILTIN_RULES: PolicyRule[] = [
  // ===========================================================================
  // Plan Mode Rules (Priority 1000+)
  // ===========================================================================

  // Plan mode: deny all by default
  {
    name: 'plan-deny-all',
    toolName: '*',
    decision: 'deny',
    priority: 1000,
    modes: [ApprovalMode.PLAN],
    source: 'builtin',
    denyMessage: 'Write operations are disabled in plan mode. Use read-only tools.',
  },
  // Plan mode: allow read tools
  {
    name: 'plan-allow-read',
    toolName: 'read_file',
    decision: 'allow',
    priority: 1001,
    modes: [ApprovalMode.PLAN],
    source: 'builtin',
  },
  {
    name: 'plan-allow-glob',
    toolName: 'glob',
    decision: 'allow',
    priority: 1001,
    modes: [ApprovalMode.PLAN],
    source: 'builtin',
  },
  {
    name: 'plan-allow-grep',
    toolName: 'grep',
    decision: 'allow',
    priority: 1001,
    modes: [ApprovalMode.PLAN],
    source: 'builtin',
  },
  {
    name: 'plan-allow-ls',
    toolName: 'ls',
    decision: 'allow',
    priority: 1001,
    modes: [ApprovalMode.PLAN],
    source: 'builtin',
  },
  {
    name: 'plan-allow-websearch',
    toolName: 'websearch',
    decision: 'allow',
    priority: 1001,
    modes: [ApprovalMode.PLAN],
    source: 'builtin',
  },
  {
    name: 'plan-allow-webfetch',
    toolName: 'webfetch',
    decision: 'allow',
    priority: 1001,
    modes: [ApprovalMode.PLAN],
    source: 'builtin',
  },
  {
    name: 'plan-allow-todoread',
    toolName: 'todoread',
    decision: 'allow',
    priority: 1001,
    modes: [ApprovalMode.PLAN],
    source: 'builtin',
  },
  {
    name: 'plan-allow-skill',
    toolName: 'skill',
    decision: 'allow',
    priority: 1001,
    modes: [ApprovalMode.PLAN],
    source: 'builtin',
  },
  {
    name: 'plan-allow-codesearch',
    toolName: 'codesearch',
    decision: 'allow',
    priority: 1001,
    modes: [ApprovalMode.PLAN],
    source: 'builtin',
  },

  // ===========================================================================
  // YOLO Mode Rules (Priority 900)
  // ===========================================================================

  {
    name: 'yolo-allow-all',
    toolName: '*',
    decision: 'allow',
    priority: 900,
    modes: [ApprovalMode.YOLO],
    source: 'builtin',
  },

  // ===========================================================================
  // Auto-Edit Mode Rules (Priority 800)
  // ===========================================================================

  {
    name: 'auto-edit-allow-write',
    toolName: 'write_file',
    decision: 'allow',
    priority: 800,
    modes: [ApprovalMode.AUTO_EDIT],
    source: 'builtin',
  },
  {
    name: 'auto-edit-allow-edit',
    toolName: 'edit',
    decision: 'allow',
    priority: 800,
    modes: [ApprovalMode.AUTO_EDIT],
    source: 'builtin',
  },
  {
    name: 'auto-edit-allow-create',
    toolName: 'create_file',
    decision: 'allow',
    priority: 800,
    modes: [ApprovalMode.AUTO_EDIT],
    source: 'builtin',
  },
  {
    name: 'auto-edit-allow-multiedit',
    toolName: 'multiedit',
    decision: 'allow',
    priority: 800,
    modes: [ApprovalMode.AUTO_EDIT],
    source: 'builtin',
  },
  {
    name: 'auto-edit-allow-apply-patch',
    toolName: 'apply_patch',
    decision: 'allow',
    priority: 800,
    modes: [ApprovalMode.AUTO_EDIT],
    source: 'builtin',
  },

  // ===========================================================================
  // Critical Safety Rules (Priority 500-600)
  // ===========================================================================

  // Block dangerous paths even in yolo mode
  {
    name: 'block-ssh-keys',
    toolName: '*',
    argsPattern: /\.ssh\/id_/,
    decision: 'deny',
    priority: 600,
    source: 'builtin',
    denyMessage: 'Cannot modify SSH private keys.',
  },
  {
    name: 'block-etc-shadow',
    toolName: '*',
    argsPattern: /\/etc\/(passwd|shadow)/,
    decision: 'deny',
    priority: 600,
    source: 'builtin',
    denyMessage: 'Cannot modify system authentication files.',
  },
  {
    name: 'block-env-files',
    toolName: 'write_file',
    argsPattern: /\.env(\.[a-z]+)?"/,
    decision: 'ask_user',
    priority: 550,
    source: 'builtin',
  },

  // ===========================================================================
  // Default Mode Rules (Priority 100-200)
  // ===========================================================================

  // Read tools: auto-approve
  {
    name: 'default-allow-read',
    toolName: 'read_file',
    decision: 'allow',
    priority: 100,
    source: 'builtin',
  },
  {
    name: 'default-allow-glob',
    toolName: 'glob',
    decision: 'allow',
    priority: 100,
    source: 'builtin',
  },
  {
    name: 'default-allow-grep',
    toolName: 'grep',
    decision: 'allow',
    priority: 100,
    source: 'builtin',
  },
  {
    name: 'default-allow-ls',
    toolName: 'ls',
    decision: 'allow',
    priority: 100,
    source: 'builtin',
  },
  {
    name: 'default-allow-todoread',
    toolName: 'todoread',
    decision: 'allow',
    priority: 100,
    source: 'builtin',
  },
  {
    name: 'default-allow-skill',
    toolName: 'skill',
    decision: 'allow',
    priority: 100,
    source: 'builtin',
  },
  {
    name: 'default-allow-codesearch',
    toolName: 'codesearch',
    decision: 'allow',
    priority: 100,
    source: 'builtin',
  },
  {
    name: 'default-allow-websearch',
    toolName: 'websearch',
    decision: 'allow',
    priority: 100,
    source: 'builtin',
  },
  {
    name: 'default-allow-webfetch',
    toolName: 'webfetch',
    decision: 'allow',
    priority: 100,
    source: 'builtin',
  },
  {
    name: 'default-allow-question',
    toolName: 'question',
    decision: 'allow',
    priority: 100,
    source: 'builtin',
  },
  {
    name: 'default-allow-attempt-completion',
    toolName: 'attempt_completion',
    decision: 'allow',
    priority: 100,
    source: 'builtin',
  },

  // Write tools: ask user
  {
    name: 'default-ask-write',
    toolName: 'write_file',
    decision: 'ask_user',
    priority: 50,
    source: 'builtin',
  },
  {
    name: 'default-ask-edit',
    toolName: 'edit',
    decision: 'ask_user',
    priority: 50,
    source: 'builtin',
  },
  {
    name: 'default-ask-create',
    toolName: 'create_file',
    decision: 'ask_user',
    priority: 50,
    source: 'builtin',
  },
  {
    name: 'default-ask-multiedit',
    toolName: 'multiedit',
    decision: 'ask_user',
    priority: 50,
    source: 'builtin',
  },
  {
    name: 'default-ask-apply-patch',
    toolName: 'apply_patch',
    decision: 'ask_user',
    priority: 50,
    source: 'builtin',
  },
  {
    name: 'default-ask-todowrite',
    toolName: 'todowrite',
    decision: 'ask_user',
    priority: 50,
    source: 'builtin',
  },

  // Delete tool: ask user
  {
    name: 'default-ask-delete',
    toolName: 'delete_file',
    decision: 'ask_user',
    priority: 50,
    source: 'builtin',
  },

  // Execute tools: ask user
  {
    name: 'default-ask-bash',
    toolName: 'bash',
    decision: 'ask_user',
    priority: 50,
    source: 'builtin',
  },
  {
    name: 'default-ask-browser',
    toolName: 'browser',
    decision: 'ask_user',
    priority: 50,
    source: 'builtin',
  },

  // MCP tools: ask user
  {
    name: 'default-ask-mcp',
    toolName: 'mcp__*',
    decision: 'ask_user',
    priority: 50,
    source: 'builtin',
  },

  // Delegation: allow (internal tools)
  {
    name: 'default-allow-task',
    toolName: 'task',
    decision: 'allow',
    priority: 100,
    source: 'builtin',
  },
  {
    name: 'default-allow-batch',
    toolName: 'batch',
    decision: 'allow',
    priority: 100,
    source: 'builtin',
  },

  // ===========================================================================
  // Fallback (Priority 0)
  // ===========================================================================

  {
    name: 'fallback-ask-all',
    toolName: '*',
    decision: 'ask_user',
    priority: 0,
    source: 'builtin',
  },
]
