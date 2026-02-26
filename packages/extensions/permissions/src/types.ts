/**
 * Permission system types.
 */

export type RiskLevel = 'low' | 'medium' | 'high' | 'critical'

export interface PermissionSettings {
  yolo: boolean
  autoApproveReads: boolean
  autoApproveWrites: boolean
  autoApproveCommands: boolean
  blockedPatterns: string[]
  trustedPaths: string[]
}

export const DEFAULT_SETTINGS: PermissionSettings = {
  yolo: false,
  autoApproveReads: true,
  autoApproveWrites: false,
  autoApproveCommands: false,
  blockedPatterns: [],
  trustedPaths: [],
}

export interface PolicyRule {
  name: string
  toolName?: string
  decision: 'allow' | 'deny' | 'ask'
  priority: number
  reason?: string
}

// ─── Risk Classification ────────────────────────────────────────────────────

const READ_TOOLS = new Set(['read_file', 'glob', 'grep', 'ls', 'codesearch', 'todoread', 'skill'])
const WRITE_TOOLS = new Set([
  'write_file',
  'create_file',
  'edit',
  'multiedit',
  'apply_patch',
  'todowrite',
])
const DELETE_TOOLS = new Set(['delete_file'])
const DANGEROUS_TOOLS = new Set(['bash', 'browser'])

export function classifyRisk(toolName: string, _args: Record<string, unknown>): RiskLevel {
  if (READ_TOOLS.has(toolName)) return 'low'
  if (WRITE_TOOLS.has(toolName)) return 'medium'
  if (DELETE_TOOLS.has(toolName)) return 'high'
  if (DANGEROUS_TOOLS.has(toolName)) return 'high'
  return 'medium'
}

// ─── Built-in Safety Rules ──────────────────────────────────────────────────

export const BUILTIN_RULES: PolicyRule[] = [
  {
    name: 'protect-git',
    toolName: '*',
    decision: 'deny',
    priority: 1000,
    reason: 'Cannot modify .git directory',
  },
  {
    name: 'protect-node-modules',
    toolName: '*',
    decision: 'deny',
    priority: 900,
    reason: 'Cannot write to node_modules',
  },
  {
    name: 'warn-env-files',
    toolName: '*',
    decision: 'ask',
    priority: 800,
    reason: 'Accessing .env files requires confirmation',
  },
  {
    name: 'deny-rm-rf-root',
    toolName: 'bash',
    decision: 'deny',
    priority: 1000,
    reason: 'Destructive rm -rf commands are blocked',
  },
  {
    name: 'warn-sudo',
    toolName: 'bash',
    decision: 'ask',
    priority: 850,
    reason: 'sudo requires confirmation',
  },
]
