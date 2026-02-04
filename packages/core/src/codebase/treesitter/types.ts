/**
 * Tree-sitter Types
 * Type definitions for tree-sitter integration
 *
 * Note: tree-sitter is an optional peer dependency
 */

// ============================================================================
// Bash Analysis Types
// ============================================================================

/**
 * Result of analyzing a bash command or script
 */
export interface BashAnalysis {
  /** Commands being executed (first word of each command) */
  commands: string[]
  /** Directories referenced in the command */
  directories: string[]
  /** Files referenced in the command */
  files: string[]
  /** Environment variables used */
  envVars: string[]
  /** Whether the command is potentially destructive */
  isDestructive: boolean
  /** Reason why it's considered destructive (if applicable) */
  destructiveReason?: string
  /** Whether the command needs sudo/root */
  needsElevation: boolean
  /** Whether the command has pipes */
  hasPipes: boolean
  /** Whether the command has redirects */
  hasRedirects: boolean
  /** Subcommands (for compound commands) */
  subcommands: string[]
  /** Raw command string */
  raw: string
}

/**
 * A detected command in bash
 */
export interface BashCommand {
  /** Command name (e.g., "rm", "git", "npm") */
  name: string
  /** Full command with arguments */
  full: string
  /** Arguments passed to the command */
  args: string[]
  /** Start position in source */
  startIndex: number
  /** End position in source */
  endIndex: number
}

/**
 * A path reference in bash
 */
export interface BashPath {
  /** The path string */
  path: string
  /** Whether it's a file or directory (heuristic) */
  type: 'file' | 'directory' | 'unknown'
  /** Start position in source */
  startIndex: number
}

// ============================================================================
// Destructive Command Patterns
// ============================================================================

/**
 * Commands that are always destructive
 */
export const DESTRUCTIVE_COMMANDS = new Set([
  'rm',
  'rmdir',
  'unlink',
  'shred',
  'truncate',
  'mkfs',
  'fdisk',
  'parted',
  'dd',
  'format',
])

/**
 * Commands that are destructive with certain flags
 */
export const CONDITIONALLY_DESTRUCTIVE: Record<string, string[]> = {
  rm: ['-r', '-rf', '-f', '--recursive', '--force'],
  git: ['reset', 'clean', 'checkout', 'restore', 'push --force'],
  npm: ['uninstall', 'remove', 'prune'],
  yarn: ['remove'],
  pnpm: ['remove', 'prune'],
  chmod: ['000', '777'], // Extreme permission changes
  chown: [], // Any chown can be problematic
}

/**
 * Commands that typically require elevated privileges
 */
export const ELEVATION_COMMANDS = new Set(['sudo', 'su', 'doas', 'pkexec'])

/**
 * Commands that modify system state
 */
export const SYSTEM_COMMANDS = new Set([
  'systemctl',
  'service',
  'apt',
  'apt-get',
  'yum',
  'dnf',
  'pacman',
  'brew',
  'snap',
  'flatpak',
  'iptables',
  'ufw',
  'firewall-cmd',
])

// ============================================================================
// Safe Command Patterns
// ============================================================================

/**
 * Commands that are safe (read-only or informational)
 */
export const SAFE_COMMANDS = new Set([
  // File listing/reading
  'ls',
  'cat',
  'head',
  'tail',
  'less',
  'more',
  'wc',
  'file',
  'stat',
  'tree',
  'find',
  'locate',
  'which',
  'whereis',
  // Text processing (read-only)
  'grep',
  'awk',
  'sed', // Can be destructive with -i, but usually safe
  'cut',
  'sort',
  'uniq',
  'diff',
  'comm',
  'join',
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
  'top',
  'ps',
  'pgrep',
  // Git (read-only)
  'git status',
  'git log',
  'git diff',
  'git show',
  'git branch',
  'git tag',
  'git remote',
  // Misc
  'echo',
  'printf',
  'true',
  'false',
  'test',
  '[',
  'env',
  'printenv',
])
