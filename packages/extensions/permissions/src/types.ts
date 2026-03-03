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
  toolRules: ToolPermissionRule[]
  smartApprove: boolean
  alwaysApproved: string[]
  /** Granular permission mode — overrides yolo/autoApproveReads/autoApproveWrites when set */
  permissionMode?: string
  /** Declarative policy rules loaded from policy files. */
  declarativePolicyRules?: DeclarativePolicyRule[]
}

export interface ToolPermissionRule {
  tool: string // Tool name or glob ('bash', 'write_*', '*')
  action: 'allow' | 'ask' | 'deny'
  paths?: string[] // Optional path restrictions (glob patterns)
  reason?: string
}

export const DEFAULT_SETTINGS: PermissionSettings = {
  yolo: false,
  autoApproveReads: true,
  autoApproveWrites: false,
  autoApproveCommands: false,
  blockedPatterns: [],
  trustedPaths: [],
  toolRules: [],
  smartApprove: false,
  alwaysApproved: [],
}

export interface PolicyRule {
  name: string
  toolName?: string
  decision: 'allow' | 'deny' | 'ask'
  priority: number
  reason?: string
}

export type PolicyDecision = 'allow' | 'deny' | 'ask'
export type PolicySource = 'builtin' | 'project' | 'user' | 'runtime'

export interface DeclarativePolicyRule {
  name: string
  tool: string
  decision: PolicyDecision
  priority: number
  source: PolicySource
  reason?: string
  argsPattern?: string
  paths?: string[]
  modes?: string[]
}

export interface DeclarativePolicyFile {
  version: number
  rules: Omit<DeclarativePolicyRule, 'source'>[]
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

// ─── Permission Request/Response (Bus Messages) ────────────────────────────

export interface PermissionRequest {
  type: 'permission:request'
  correlationId: string
  timestamp: number
  toolName: string
  args: Record<string, unknown>
  risk: RiskLevel
}

export interface PermissionResponse {
  type: 'permission:response'
  correlationId: string
  timestamp: number
  approved: boolean
  reason?: string
  alwaysApprove?: boolean
}

// ─── Safe Bash Patterns (for smartApprove) ──────────────────────────────────

export const SAFE_BASH_PATTERNS: RegExp[] = [
  /^ls\b/,
  /^cat\b/,
  /^head\b/,
  /^tail\b/,
  /^wc\b/,
  /^echo\b/,
  /^pwd$/,
  /^git\s+(status|log|diff|branch|show|rev-parse)\b/,
  /^npm\s+(test|run\s+test|run\s+lint)\b/,
  /^pnpm\s+(test|run\s+test|lint)\b/,
  /^npx\s+(vitest|tsc|biome|oxlint)\b/,
  /^node\s+--version$/,
  /^which\b/,
  /^rg\b/,
  /^grep\b/,
]

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
