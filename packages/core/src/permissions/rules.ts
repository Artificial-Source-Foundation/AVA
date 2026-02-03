/**
 * Built-in Permission Rules
 * Default rules for common dangerous operations
 */

import type { PermissionRule, RiskLevel } from './types.js'

// ============================================================================
// Built-in Rules
// ============================================================================

/** Built-in rules that cannot be removed */
export const BUILTIN_RULES: PermissionRule[] = [
  // Git protection - never allow modifying .git
  {
    id: 'builtin:protect-git',
    pattern: '**/.git/**',
    action: 'deny',
    scope: 'persistent',
    reason: 'Protect git history from accidental modification',
    priority: 1000,
    builtin: true,
  },

  // Node modules protection
  {
    id: 'builtin:protect-node-modules',
    pattern: '**/node_modules/**',
    action: 'deny',
    scope: 'persistent',
    actionType: 'write',
    reason: 'Dependencies should not be modified directly',
    priority: 900,
    builtin: true,
  },
  {
    id: 'builtin:protect-node-modules-delete',
    pattern: '**/node_modules/**',
    action: 'deny',
    scope: 'persistent',
    actionType: 'delete',
    reason: 'Dependencies should not be modified directly',
    priority: 900,
    builtin: true,
  },

  // Environment files - ask before modifying
  {
    id: 'builtin:warn-env-files',
    pattern: '**/.env*',
    action: 'ask',
    scope: 'session',
    reason: 'Environment files may contain secrets',
    priority: 800,
    builtin: true,
  },

  // Dangerous shell commands
  {
    id: 'builtin:deny-rm-rf-root',
    pattern: 'rm -rf /',
    action: 'deny',
    scope: 'persistent',
    tool: 'bash',
    reason: 'Catastrophically dangerous command',
    priority: 1000,
    builtin: true,
  },
  {
    id: 'builtin:deny-rm-rf-star',
    pattern: 'rm -rf *',
    action: 'deny',
    scope: 'persistent',
    tool: 'bash',
    reason: 'Too dangerous without explicit path',
    priority: 1000,
    builtin: true,
  },
  {
    id: 'builtin:deny-rm-rf-dot',
    pattern: 'rm -rf .',
    action: 'deny',
    scope: 'persistent',
    tool: 'bash',
    reason: 'Would delete current directory',
    priority: 1000,
    builtin: true,
  },

  // Sudo commands
  {
    id: 'builtin:ask-sudo',
    pattern: 'sudo *',
    action: 'ask',
    scope: 'once',
    tool: 'bash',
    reason: 'Elevated privileges required',
    priority: 850,
    builtin: true,
  },

  // Package installation
  {
    id: 'builtin:ask-npm-install',
    pattern: 'npm install*',
    action: 'ask',
    scope: 'session',
    tool: 'bash',
    reason: 'Installing packages modifies node_modules',
    priority: 700,
    builtin: true,
  },
  {
    id: 'builtin:ask-pnpm-install',
    pattern: 'pnpm install*',
    action: 'ask',
    scope: 'session',
    tool: 'bash',
    reason: 'Installing packages modifies node_modules',
    priority: 700,
    builtin: true,
  },
  {
    id: 'builtin:ask-yarn-add',
    pattern: 'yarn add*',
    action: 'ask',
    scope: 'session',
    tool: 'bash',
    reason: 'Installing packages modifies node_modules',
    priority: 700,
    builtin: true,
  },

  // Git force operations
  {
    id: 'builtin:ask-git-push-force',
    pattern: 'git push*--force*',
    action: 'ask',
    scope: 'once',
    tool: 'bash',
    reason: 'Force push can overwrite remote history',
    priority: 850,
    builtin: true,
  },
  {
    id: 'builtin:ask-git-reset-hard',
    pattern: 'git reset --hard*',
    action: 'ask',
    scope: 'once',
    tool: 'bash',
    reason: 'Hard reset discards uncommitted changes',
    priority: 850,
    builtin: true,
  },
]

// ============================================================================
// Risk Assessment
// ============================================================================

/** Patterns that indicate high/critical risk operations */
const RISK_PATTERNS: { pattern: RegExp; risk: RiskLevel; reason: string }[] = [
  // Critical - system-level danger
  { pattern: /^rm\s+-rf\s+\//, risk: 'critical', reason: 'Recursive delete from root' },
  { pattern: /^dd\s+.*of=\/dev\//, risk: 'critical', reason: 'Direct disk write' },
  { pattern: /^mkfs/, risk: 'critical', reason: 'Filesystem format' },
  { pattern: /^:(){ :|:& };:/, risk: 'critical', reason: 'Fork bomb' },

  // High - data loss potential
  { pattern: /^rm\s+-rf/, risk: 'high', reason: 'Recursive force delete' },
  { pattern: /^rm\s+.*\*/, risk: 'high', reason: 'Wildcard delete' },
  { pattern: /^git\s+push.*--force/, risk: 'high', reason: 'Force push' },
  { pattern: /^git\s+reset\s+--hard/, risk: 'high', reason: 'Hard reset' },
  { pattern: /^chmod\s+777/, risk: 'high', reason: 'Insecure permissions' },
  { pattern: /^chown\s+-R/, risk: 'high', reason: 'Recursive ownership change' },

  // Medium - needs attention
  { pattern: /^sudo\s+/, risk: 'medium', reason: 'Elevated privileges' },
  { pattern: /^npm\s+install/, risk: 'medium', reason: 'Package installation' },
  { pattern: /^pnpm\s+install/, risk: 'medium', reason: 'Package installation' },
  { pattern: /^yarn\s+add/, risk: 'medium', reason: 'Package installation' },
  { pattern: /^curl.*\|\s*sh/, risk: 'medium', reason: 'Piped script execution' },
  { pattern: /^wget.*\|\s*sh/, risk: 'medium', reason: 'Piped script execution' },
]

/** File path patterns that indicate elevated risk */
const PATH_RISK_PATTERNS: { pattern: RegExp; risk: RiskLevel; reason: string }[] = [
  { pattern: /^\/etc\//, risk: 'high', reason: 'System configuration' },
  { pattern: /^\/usr\//, risk: 'high', reason: 'System files' },
  { pattern: /^\/var\//, risk: 'medium', reason: 'Variable data' },
  { pattern: /^~\/\.ssh\//, risk: 'high', reason: 'SSH keys' },
  { pattern: /^~\/\.gnupg\//, risk: 'high', reason: 'GPG keys' },
  { pattern: /\.env/, risk: 'medium', reason: 'Environment variables' },
  { pattern: /\.pem$/, risk: 'high', reason: 'Private key' },
  { pattern: /\.key$/, risk: 'high', reason: 'Private key' },
  { pattern: /id_rsa/, risk: 'high', reason: 'SSH private key' },
  { pattern: /\.git\//, risk: 'high', reason: 'Git internals' },
]

/**
 * Assess the risk level of a command
 */
export function assessCommandRisk(command: string): { risk: RiskLevel; reason: string } {
  const normalizedCommand = command.trim().toLowerCase()

  for (const { pattern, risk, reason } of RISK_PATTERNS) {
    if (pattern.test(normalizedCommand)) {
      return { risk, reason }
    }
  }

  return { risk: 'low', reason: 'Standard command' }
}

/**
 * Assess the risk level of a file path operation
 */
export function assessPathRisk(path: string): { risk: RiskLevel; reason: string } {
  for (const { pattern, risk, reason } of PATH_RISK_PATTERNS) {
    if (pattern.test(path)) {
      return { risk, reason }
    }
  }

  return { risk: 'low', reason: 'Standard path' }
}

/**
 * Get the highest risk from multiple paths
 */
export function getHighestPathRisk(paths: string[]): { risk: RiskLevel; reason: string } {
  const riskOrder: RiskLevel[] = ['low', 'medium', 'high', 'critical']
  let highest = { risk: 'low' as RiskLevel, reason: 'Standard operation' }

  for (const path of paths) {
    const pathRisk = assessPathRisk(path)
    if (riskOrder.indexOf(pathRisk.risk) > riskOrder.indexOf(highest.risk)) {
      highest = pathRisk
    }
  }

  return highest
}
