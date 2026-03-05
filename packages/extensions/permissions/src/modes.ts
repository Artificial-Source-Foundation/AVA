/**
 * Granular permission modes — 5 levels matching competitive tools.
 */

export type PermissionMode = 'suggest' | 'ask' | 'auto-edit' | 'auto-safe' | 'yolo'

export interface PermissionModeConfig {
  name: PermissionMode
  description: string
  autoApprove: Set<string> // tool categories that are auto-approved
  requireApproval: Set<string> // tool categories that always require approval
}

// Tool categories for permission checking
const READ_TOOLS = new Set([
  'read_file',
  'glob',
  'grep',
  'ls',
  'todoread',
  'memory_read',
  'memory_list',
  'lsp_diagnostics',
  'lsp_hover',
  'lsp_definition',
])

const EDIT_TOOLS = new Set([
  'write_file',
  'edit',
  'create_file',
  'multiedit',
  'apply_patch',
  'todowrite',
  'memory_write',
  'memory_delete',
])

const EXECUTE_TOOLS = new Set(['bash'])

const NETWORK_TOOLS = new Set(['websearch', 'webfetch'])

const DESTRUCTIVE_TOOLS = new Set(['delete_file'])

const META_TOOLS = new Set([
  'question',
  'attempt_completion',
  'plan_enter',
  'plan_exit',
  'batch',
  'task',
])

export const PERMISSION_MODES: Record<PermissionMode, PermissionModeConfig> = {
  suggest: {
    name: 'suggest',
    description: 'Suggest only — never execute any tool',
    autoApprove: new Set(),
    requireApproval: new Set(['*']),
  },
  ask: {
    name: 'ask',
    description: 'Ask for every tool call (default)',
    autoApprove: META_TOOLS,
    requireApproval: new Set(['*']),
  },
  'auto-edit': {
    name: 'auto-edit',
    description: 'Auto-approve reads + edits, ask for bash/delete/network',
    autoApprove: new Set([...READ_TOOLS, ...EDIT_TOOLS, ...META_TOOLS]),
    requireApproval: new Set([...EXECUTE_TOOLS, ...DESTRUCTIVE_TOOLS, ...NETWORK_TOOLS]),
  },
  'auto-safe': {
    name: 'auto-safe',
    description: 'Auto-approve reads + edits + safe bash, ask for delete/network',
    autoApprove: new Set([...READ_TOOLS, ...EDIT_TOOLS, ...META_TOOLS]),
    requireApproval: new Set([...EXECUTE_TOOLS, ...DESTRUCTIVE_TOOLS, ...NETWORK_TOOLS]),
  },
  yolo: {
    name: 'yolo',
    description: 'Auto-approve everything',
    autoApprove: new Set(['*']),
    requireApproval: new Set(),
  },
}

/** Map frontend permission mode names to backend modes. */
const MODE_ALIASES: Record<string, PermissionMode> = {
  bypass: 'yolo',
  'auto-approve': 'auto-edit',
}

export function isToolAutoApproved(toolName: string, mode: PermissionMode | string): boolean {
  const resolvedMode = MODE_ALIASES[mode] ?? mode
  const config = PERMISSION_MODES[resolvedMode as PermissionMode]
  if (!config) return true // Unknown mode — fail open (bypass behavior)
  if (config.autoApprove.has('*')) return true
  if (config.requireApproval.has('*') && !config.autoApprove.has(toolName)) return false
  return config.autoApprove.has(toolName) && !config.requireApproval.has(toolName)
}

export function getPermissionMode(name: string): PermissionModeConfig | undefined {
  return PERMISSION_MODES[name as PermissionMode]
}

export function getAllPermissionModes(): PermissionModeConfig[] {
  return Object.values(PERMISSION_MODES)
}

export { READ_TOOLS, EDIT_TOOLS, EXECUTE_TOOLS, NETWORK_TOOLS, DESTRUCTIVE_TOOLS, META_TOOLS }
