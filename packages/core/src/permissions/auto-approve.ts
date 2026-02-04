/**
 * Auto-Approval System
 * Configurable automatic approval for tool operations
 *
 * Supports:
 * - Granular action-level controls
 * - Path-aware checking (local vs external workspace)
 * - Yolo mode for unrestricted operation
 */

import { isAbsolute, normalize, relative, resolve } from 'path'
import type { PermissionAction, RiskLevel } from './types.js'

// ============================================================================
// Types
// ============================================================================

/**
 * Settings for auto-approval of different action types
 */
export interface AutoApprovalSettings {
  /** Master toggle for auto-approval system */
  enabled: boolean

  /**
   * YOLO mode: Auto-approve ALL operations
   * WARNING: This is dangerous and should only be used in trusted environments
   */
  yolo: boolean

  /** Granular action controls */
  actions: AutoApprovalActions

  /** Workspace root path (operations outside this are "external") */
  workspaceRoot?: string

  /** Maximum risk level to auto-approve (default: medium) */
  maxRiskLevel: RiskLevel

  /** Additional trusted paths outside workspace */
  trustedPaths: string[]

  /** Patterns to always block (even in yolo mode) */
  blockedPatterns: string[]
}

/**
 * Granular controls for each action type
 */
export interface AutoApprovalActions {
  /** Auto-approve reading files in workspace */
  readFiles: boolean
  /** Auto-approve reading files outside workspace */
  readFilesExternally: boolean
  /** Auto-approve writing/editing files in workspace */
  editFiles: boolean
  /** Auto-approve writing/editing files outside workspace */
  editFilesExternally: boolean
  /** Auto-approve creating new files in workspace */
  createFiles: boolean
  /** Auto-approve creating new files outside workspace */
  createFilesExternally: boolean
  /** Auto-approve deleting files in workspace */
  deleteFiles: boolean
  /** Auto-approve deleting files outside workspace */
  deleteFilesExternally: boolean
  /** Auto-approve "safe" shell commands (ls, cat, etc.) */
  executeSafeCommands: boolean
  /** Auto-approve ALL shell commands (DANGEROUS) */
  executeAllCommands: boolean
  /** Auto-approve browser automation */
  useBrowser: boolean
  /** Auto-approve MCP tool calls */
  useMcp: boolean
  /** Auto-approve web fetching */
  useWebFetch: boolean
}

/**
 * Result of checking auto-approval
 */
export interface AutoApprovalResult {
  /** Whether the operation is auto-approved */
  approved: boolean
  /** Reason for the decision */
  reason: string
  /** Whether the path is within workspace */
  isLocal?: boolean
}

// ============================================================================
// Default Configuration
// ============================================================================

/**
 * Default auto-approval settings (conservative)
 */
export const DEFAULT_AUTO_APPROVAL_SETTINGS: AutoApprovalSettings = {
  enabled: true,
  yolo: false,
  actions: {
    readFiles: true,
    readFilesExternally: false,
    editFiles: false,
    editFilesExternally: false,
    createFiles: false,
    createFilesExternally: false,
    deleteFiles: false,
    deleteFilesExternally: false,
    executeSafeCommands: false,
    executeAllCommands: false,
    useBrowser: false,
    useMcp: false,
    useWebFetch: true,
  },
  maxRiskLevel: 'medium',
  trustedPaths: [],
  blockedPatterns: [
    // Sensitive system paths
    '/etc/passwd',
    '/etc/shadow',
    '~/.ssh/*',
    '**/.env',
    '**/.env.*',
    '**/secrets*',
    '**/credentials*',
    // Git internals
    '**/.git/config',
    '**/.git/objects/**',
    // Package manager sensitive
    '**/node_modules/.bin/**',
  ],
}

/**
 * Yolo mode preset (approves everything except blocked patterns)
 */
export const YOLO_AUTO_APPROVAL_SETTINGS: AutoApprovalSettings = {
  enabled: true,
  yolo: true,
  actions: {
    readFiles: true,
    readFilesExternally: true,
    editFiles: true,
    editFilesExternally: true,
    createFiles: true,
    createFilesExternally: true,
    deleteFiles: true,
    deleteFilesExternally: true,
    executeSafeCommands: true,
    executeAllCommands: true,
    useBrowser: true,
    useMcp: true,
    useWebFetch: true,
  },
  maxRiskLevel: 'high',
  trustedPaths: [],
  blockedPatterns: [
    // Even yolo mode blocks these
    '/etc/passwd',
    '/etc/shadow',
    '**/.ssh/id_*',
  ],
}

// ============================================================================
// Safe Commands
// ============================================================================

/**
 * Commands that are considered safe (read-only or informational)
 */
export const AUTO_APPROVE_SAFE_COMMANDS: readonly string[] = [
  // File listing
  'ls',
  'dir',
  'find',
  'fd',
  'tree',
  // File reading
  'cat',
  'head',
  'tail',
  'less',
  'more',
  'bat',
  // File searching
  'grep',
  'rg',
  'ag',
  'ack',
  // Git (read-only)
  'git status',
  'git log',
  'git diff',
  'git branch',
  'git show',
  'git remote',
  // System info
  'pwd',
  'whoami',
  'hostname',
  'uname',
  'date',
  'uptime',
  'df',
  'du',
  'free',
  'which',
  'type',
  'command -v',
  // Package info
  'npm ls',
  'npm list',
  'npm outdated',
  'npm view',
  'pnpm ls',
  'pnpm list',
  'yarn list',
  // Environment
  'echo',
  'printf',
  'env',
  'printenv',
  // Node
  'node --version',
  'node -v',
  'npm --version',
  'npm -v',
  // Misc read-only
  'wc',
  'sort',
  'uniq',
  'cut',
  'awk',
  'sed',
  'jq',
  'yq',
] as const

// ============================================================================
// Path Checking
// ============================================================================

/**
 * Check if a path is within the workspace
 */
export function isPathLocal(filePath: string, workspaceRoot: string): boolean {
  const absPath = isAbsolute(filePath) ? filePath : resolve(workspaceRoot, filePath)
  const normalizedPath = normalize(absPath)
  const normalizedRoot = normalize(workspaceRoot)

  // Check if path is under workspace root
  const rel = relative(normalizedRoot, normalizedPath)
  return !rel.startsWith('..') && !isAbsolute(rel)
}

/**
 * Check if a path is in a trusted location
 */
export function isPathTrusted(
  filePath: string,
  workspaceRoot: string,
  trustedPaths: string[]
): boolean {
  // First check if it's local
  if (isPathLocal(filePath, workspaceRoot)) {
    return true
  }

  // Then check trusted paths
  const absPath = isAbsolute(filePath) ? filePath : resolve(workspaceRoot, filePath)
  const normalizedPath = normalize(absPath)

  for (const trusted of trustedPaths) {
    const normalizedTrusted = normalize(
      isAbsolute(trusted) ? trusted : resolve(workspaceRoot, trusted)
    )

    // Check if path is under trusted path
    const rel = relative(normalizedTrusted, normalizedPath)
    if (!rel.startsWith('..') && !isAbsolute(rel)) {
      return true
    }
  }

  return false
}

/**
 * Check if a path matches any blocked pattern
 */
export function isPathBlocked(filePath: string, blockedPatterns: string[]): boolean {
  const normalizedPath = normalize(filePath)

  for (const pattern of blockedPatterns) {
    // Simple glob matching
    if (matchGlob(normalizedPath, pattern)) {
      return true
    }
  }

  return false
}

/**
 * Simple glob pattern matching
 */
function matchGlob(path: string, pattern: string): boolean {
  // Convert glob to regex
  let regexStr = pattern
    .replace(/[.+^${}()|[\]\\]/g, '\\$&') // Escape special regex chars
    .replace(/\*\*/g, '<<<DOUBLE_STAR>>>') // Temporarily replace **
    .replace(/\*/g, '[^/]*') // * matches anything except /
    .replace(/<<<DOUBLE_STAR>>>/g, '.*') // ** matches anything including /
    .replace(/\?/g, '.') // ? matches single char

  // Handle home directory expansion
  if (regexStr.startsWith('~')) {
    const home = process.env.HOME ?? process.env.USERPROFILE ?? ''
    regexStr = home.replace(/[.+^${}()|[\]\\]/g, '\\$&') + regexStr.slice(1)
  }

  const regex = new RegExp(`^${regexStr}$`)
  return regex.test(path)
}

// ============================================================================
// Command Checking
// ============================================================================

/**
 * Check if a command is considered safe
 */
export function isCommandSafe(command: string): boolean {
  const trimmed = command.trim().toLowerCase()

  for (const safe of AUTO_APPROVE_SAFE_COMMANDS) {
    // Check if command starts with safe command
    if (trimmed === safe || trimmed.startsWith(`${safe} `) || trimmed.startsWith(`${safe}\t`)) {
      return true
    }
  }

  return false
}

/**
 * Extract the base command from a command string
 */
export function extractBaseCommand(command: string): string {
  const trimmed = command.trim()

  // Handle pipes, redirects, etc.
  const parts = trimmed.split(/[|;&]/)
  const firstPart = (parts[0] ?? trimmed).trim()

  // Extract first word
  const words = firstPart.split(/\s/)
  const firstWord = words[0] ?? firstPart

  return firstWord
}

// ============================================================================
// Auto-Approval Logic
// ============================================================================

/**
 * Check if a file operation should be auto-approved
 */
export function checkFileAutoApproval(
  action: 'read' | 'write' | 'delete',
  filePath: string,
  settings: AutoApprovalSettings
): AutoApprovalResult {
  // System disabled
  if (!settings.enabled) {
    return { approved: false, reason: 'Auto-approval disabled' }
  }

  // Check blocked patterns (even in yolo mode)
  if (isPathBlocked(filePath, settings.blockedPatterns)) {
    return { approved: false, reason: 'Path matches blocked pattern' }
  }

  // Yolo mode approves everything not blocked
  if (settings.yolo) {
    return { approved: true, reason: 'Yolo mode enabled' }
  }

  // Check path locality
  const workspaceRoot = settings.workspaceRoot ?? process.cwd()
  const isLocal = isPathTrusted(filePath, workspaceRoot, settings.trustedPaths)

  // Check action-specific settings
  switch (action) {
    case 'read':
      if (isLocal && settings.actions.readFiles) {
        return { approved: true, reason: 'Read files in workspace', isLocal: true }
      }
      if (!isLocal && settings.actions.readFilesExternally) {
        return { approved: true, reason: 'Read files externally enabled', isLocal: false }
      }
      break

    case 'write':
      if (isLocal && settings.actions.editFiles) {
        return { approved: true, reason: 'Edit files in workspace', isLocal: true }
      }
      if (!isLocal && settings.actions.editFilesExternally) {
        return { approved: true, reason: 'Edit files externally enabled', isLocal: false }
      }
      break

    case 'delete':
      if (isLocal && settings.actions.deleteFiles) {
        return { approved: true, reason: 'Delete files in workspace', isLocal: true }
      }
      if (!isLocal && settings.actions.deleteFilesExternally) {
        return { approved: true, reason: 'Delete files externally enabled', isLocal: false }
      }
      break
  }

  return {
    approved: false,
    reason: `${action} not auto-approved for ${isLocal ? 'local' : 'external'} paths`,
    isLocal,
  }
}

/**
 * Check if a command execution should be auto-approved
 */
export function checkCommandAutoApproval(
  command: string,
  settings: AutoApprovalSettings
): AutoApprovalResult {
  // System disabled
  if (!settings.enabled) {
    return { approved: false, reason: 'Auto-approval disabled' }
  }

  // Yolo mode approves everything
  if (settings.yolo) {
    return { approved: true, reason: 'Yolo mode enabled' }
  }

  // All commands allowed
  if (settings.actions.executeAllCommands) {
    return { approved: true, reason: 'Execute all commands enabled' }
  }

  // Check if safe command
  if (settings.actions.executeSafeCommands && isCommandSafe(command)) {
    return { approved: true, reason: 'Safe command' }
  }

  return { approved: false, reason: 'Command not in safe list and executeAllCommands disabled' }
}

/**
 * Check if a browser operation should be auto-approved
 */
export function checkBrowserAutoApproval(settings: AutoApprovalSettings): AutoApprovalResult {
  if (!settings.enabled) {
    return { approved: false, reason: 'Auto-approval disabled' }
  }

  if (settings.yolo || settings.actions.useBrowser) {
    return { approved: true, reason: 'Browser usage enabled' }
  }

  return { approved: false, reason: 'Browser usage not auto-approved' }
}

/**
 * Check if an MCP operation should be auto-approved
 */
export function checkMcpAutoApproval(settings: AutoApprovalSettings): AutoApprovalResult {
  if (!settings.enabled) {
    return { approved: false, reason: 'Auto-approval disabled' }
  }

  if (settings.yolo || settings.actions.useMcp) {
    return { approved: true, reason: 'MCP usage enabled' }
  }

  return { approved: false, reason: 'MCP usage not auto-approved' }
}

/**
 * Check if a web fetch operation should be auto-approved
 */
export function checkWebFetchAutoApproval(settings: AutoApprovalSettings): AutoApprovalResult {
  if (!settings.enabled) {
    return { approved: false, reason: 'Auto-approval disabled' }
  }

  if (settings.yolo || settings.actions.useWebFetch) {
    return { approved: true, reason: 'Web fetch enabled' }
  }

  return { approved: false, reason: 'Web fetch not auto-approved' }
}

// ============================================================================
// State Management
// ============================================================================

/** Current auto-approval settings */
let currentSettings: AutoApprovalSettings = { ...DEFAULT_AUTO_APPROVAL_SETTINGS }

/**
 * Get current auto-approval settings
 */
export function getAutoApprovalSettings(): AutoApprovalSettings {
  return { ...currentSettings }
}

/**
 * Update auto-approval settings
 */
export function setAutoApprovalSettings(
  settings: Partial<AutoApprovalSettings>
): AutoApprovalSettings {
  currentSettings = {
    ...currentSettings,
    ...settings,
    actions: {
      ...currentSettings.actions,
      ...settings.actions,
    },
  }
  return { ...currentSettings }
}

/**
 * Reset to default settings
 */
export function resetAutoApprovalSettings(): AutoApprovalSettings {
  currentSettings = { ...DEFAULT_AUTO_APPROVAL_SETTINGS }
  return { ...currentSettings }
}

/**
 * Enable yolo mode
 */
export function enableYoloMode(): AutoApprovalSettings {
  return setAutoApprovalSettings(YOLO_AUTO_APPROVAL_SETTINGS)
}

/**
 * Disable yolo mode (return to defaults)
 */
export function disableYoloMode(): AutoApprovalSettings {
  return resetAutoApprovalSettings()
}

// ============================================================================
// Convenience Functions
// ============================================================================

/**
 * Quick check if an operation should be auto-approved
 * Combines all checks based on tool and action type
 */
export function shouldAutoApprove(
  tool: string,
  action: PermissionAction,
  context: { path?: string; command?: string }
): AutoApprovalResult {
  const settings = getAutoApprovalSettings()

  // Handle by tool/action type
  switch (tool) {
    case 'read':
    case 'glob':
    case 'grep':
    case 'ls':
      return checkFileAutoApproval('read', context.path ?? '', settings)

    case 'write':
    case 'edit':
    case 'create':
      return checkFileAutoApproval('write', context.path ?? '', settings)

    case 'delete':
      return checkFileAutoApproval('delete', context.path ?? '', settings)

    case 'bash':
      return checkCommandAutoApproval(context.command ?? '', settings)

    case 'browser':
      return checkBrowserAutoApproval(settings)

    case 'webfetch':
    case 'websearch':
      return checkWebFetchAutoApproval(settings)

    default:
      // Check if MCP-like
      if (tool.startsWith('mcp_') || tool.includes(':')) {
        return checkMcpAutoApproval(settings)
      }

      // Default: check by action
      if (action === 'read') {
        return checkFileAutoApproval('read', context.path ?? '', settings)
      }
      if (action === 'write') {
        return checkFileAutoApproval('write', context.path ?? '', settings)
      }
      if (action === 'execute') {
        return checkCommandAutoApproval(context.command ?? '', settings)
      }

      return { approved: false, reason: `Unknown tool: ${tool}` }
  }
}
