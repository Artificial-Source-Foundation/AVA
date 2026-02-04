/**
 * Bash Analysis
 * Extract information from bash commands and scripts
 *
 * Uses regex-based parsing (tree-sitter optional for future enhancement)
 */

import {
  type BashAnalysis,
  type BashCommand,
  CONDITIONALLY_DESTRUCTIVE,
  DESTRUCTIVE_COMMANDS,
  ELEVATION_COMMANDS,
  SAFE_COMMANDS,
  SYSTEM_COMMANDS,
} from './types.js'

// ============================================================================
// Regex Patterns
// ============================================================================

/** Match command at start of line or after certain operators */
const COMMAND_PATTERN = /(?:^|;|&&|\|\||`|\$\()\s*([a-zA-Z0-9_./-]+)/g

/** Match environment variable usage */
const ENV_VAR_PATTERN = /\$([A-Za-z_][A-Za-z0-9_]*)/g

/** Match paths (simplified) */
const PATH_PATTERN = /(?:^|\s)((?:\/[^\s;|&<>]+)|(?:\.\.?\/[^\s;|&<>]+)|(?:~\/[^\s;|&<>]+))/g

/** Match file-like arguments (not starting with -) */
const FILE_ARG_PATTERN = /\s+([^\s-][^\s;|&<>]*\.[a-zA-Z0-9]+)/g

/** Match pipe operator */
const PIPE_PATTERN = /\|(?!\|)/

/** Match redirect operators */
const REDIRECT_PATTERN = /(?:>>?|<<|<)/

// ============================================================================
// Main Analysis Function
// ============================================================================

/**
 * Analyze a bash command string
 *
 * Extracts:
 * - Commands being executed
 * - Files and directories referenced
 * - Environment variables used
 * - Whether the command is destructive
 */
export function analyzeBash(command: string): BashAnalysis {
  const trimmed = command.trim()

  // Extract commands
  const commands = extractCommands(trimmed)

  // Extract environment variables
  const envVars = extractEnvVars(trimmed)

  // Extract paths
  const { files, directories } = extractPaths(trimmed)

  // Check for destructive patterns
  const destructive = checkDestructive(trimmed, commands)

  // Check for elevation
  const needsElevation = checkElevation(commands)

  // Check for pipes and redirects
  const hasPipes = PIPE_PATTERN.test(trimmed)
  const hasRedirects = REDIRECT_PATTERN.test(trimmed)

  // Extract subcommands (for compound commands like git commit)
  const subcommands = extractSubcommands(trimmed, commands)

  return {
    commands: commands.map((c) => c.name),
    directories,
    files,
    envVars,
    isDestructive: destructive.isDestructive,
    destructiveReason: destructive.reason,
    needsElevation,
    hasPipes,
    hasRedirects,
    subcommands,
    raw: trimmed,
  }
}

// ============================================================================
// Extraction Functions
// ============================================================================

/**
 * Extract command names from a bash string
 */
function extractCommands(command: string): BashCommand[] {
  const commands: BashCommand[] = []
  const seen = new Set<string>()

  const matches = command.matchAll(COMMAND_PATTERN)
  for (const match of matches) {
    const name = match[1]

    // Skip if we've seen this command or it looks like a path/variable
    if (seen.has(name) || name.startsWith('$') || name.includes('/')) {
      continue
    }

    // Skip common shell builtins that aren't real commands
    if (['then', 'else', 'fi', 'do', 'done', 'esac'].includes(name)) {
      continue
    }

    seen.add(name)
    commands.push({
      name,
      full: extractFullCommand(command, match.index ?? 0, name),
      args: extractArgs(command, match.index ?? 0, name),
      startIndex: match.index ?? 0,
      endIndex: (match.index ?? 0) + name.length,
    })
  }

  return commands
}

/**
 * Extract the full command including arguments
 */
function extractFullCommand(source: string, startIndex: number, cmdName: string): string {
  // Find where this command starts (after any prefix)
  const cmdStart = source.indexOf(cmdName, startIndex)
  if (cmdStart === -1) return cmdName

  // Find where it ends (next operator or end of string)
  const rest = source.slice(cmdStart)
  const endMatch = rest.search(/[;|&\n]/)
  const endIndex = endMatch === -1 ? rest.length : endMatch

  return rest.slice(0, endIndex).trim()
}

/**
 * Extract arguments for a command
 */
function extractArgs(source: string, startIndex: number, cmdName: string): string[] {
  const full = extractFullCommand(source, startIndex, cmdName)
  const parts = full.split(/\s+/).slice(1) // Skip command name
  return parts.filter((p) => p.length > 0)
}

/**
 * Extract environment variables
 */
function extractEnvVars(command: string): string[] {
  const vars: string[] = []
  const seen = new Set<string>()

  const matches = command.matchAll(ENV_VAR_PATTERN)
  for (const match of matches) {
    const name = match[1]
    if (!seen.has(name)) {
      seen.add(name)
      vars.push(name)
    }
  }

  return vars
}

/**
 * Extract file and directory paths
 */
function extractPaths(command: string): { files: string[]; directories: string[] } {
  const files: string[] = []
  const directories: string[] = []
  const seen = new Set<string>()

  // Extract absolute and relative paths
  const pathMatches = command.matchAll(PATH_PATTERN)
  for (const match of pathMatches) {
    const path = match[1].trim()
    if (seen.has(path)) continue
    seen.add(path)

    // Heuristic: ends with / or no extension = directory
    if (path.endsWith('/') || !path.includes('.')) {
      directories.push(path)
    } else {
      files.push(path)
    }
  }

  // Extract file-like arguments
  const fileMatches = command.matchAll(FILE_ARG_PATTERN)
  for (const match of fileMatches) {
    const path = match[1].trim()
    if (!seen.has(path)) {
      seen.add(path)
      files.push(path)
    }
  }

  return { files, directories }
}

/**
 * Extract subcommands (for tools like git, npm, docker)
 */
function extractSubcommands(_command: string, commands: BashCommand[]): string[] {
  const subcommands: string[] = []

  for (const cmd of commands) {
    // Tools with subcommands
    if (
      ['git', 'npm', 'npx', 'yarn', 'pnpm', 'docker', 'kubectl', 'aws', 'gcloud'].includes(cmd.name)
    ) {
      // First non-flag argument is usually the subcommand
      for (const arg of cmd.args) {
        if (!arg.startsWith('-')) {
          subcommands.push(`${cmd.name} ${arg}`)
          break
        }
      }
    }
  }

  return subcommands
}

// ============================================================================
// Risk Assessment
// ============================================================================

/**
 * Check if a command is destructive
 */
function checkDestructive(
  command: string,
  commands: BashCommand[]
): { isDestructive: boolean; reason?: string } {
  for (const cmd of commands) {
    // Always destructive commands
    if (DESTRUCTIVE_COMMANDS.has(cmd.name)) {
      return {
        isDestructive: true,
        reason: `${cmd.name} is a destructive command`,
      }
    }

    // Conditionally destructive
    const conditionalFlags = CONDITIONALLY_DESTRUCTIVE[cmd.name]
    if (conditionalFlags) {
      for (const flag of conditionalFlags) {
        if (cmd.args.includes(flag) || cmd.full.includes(flag)) {
          return {
            isDestructive: true,
            reason: `${cmd.name} with ${flag} is destructive`,
          }
        }
      }
    }

    // System commands
    if (SYSTEM_COMMANDS.has(cmd.name)) {
      return {
        isDestructive: true,
        reason: `${cmd.name} modifies system state`,
      }
    }

    // rm with glob patterns
    if (cmd.name === 'rm' && (command.includes('*') || command.includes('?'))) {
      return {
        isDestructive: true,
        reason: 'rm with glob patterns is risky',
      }
    }
  }

  return { isDestructive: false }
}

/**
 * Check if command needs elevated privileges
 */
function checkElevation(commands: BashCommand[]): boolean {
  return commands.some((cmd) => ELEVATION_COMMANDS.has(cmd.name))
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Check if a command is safe (read-only)
 */
export function isSafeCommand(command: string): boolean {
  const analysis = analyzeBash(command)

  // If destructive, not safe
  if (analysis.isDestructive) return false

  // If needs elevation, not safe
  if (analysis.needsElevation) return false

  // If all commands are in safe list, it's safe
  return analysis.commands.every((cmd) => SAFE_COMMANDS.has(cmd))
}

/**
 * Get a summary of command risk
 */
export function getCommandRiskSummary(command: string): {
  risk: 'safe' | 'low' | 'medium' | 'high' | 'critical'
  reasons: string[]
} {
  const analysis = analyzeBash(command)
  const reasons: string[] = []

  if (analysis.needsElevation) {
    reasons.push('Requires elevated privileges')
  }

  if (analysis.isDestructive) {
    reasons.push(analysis.destructiveReason ?? 'Destructive command')
  }

  if (analysis.commands.some((c) => SYSTEM_COMMANDS.has(c))) {
    reasons.push('Modifies system state')
  }

  // Determine risk level
  let risk: 'safe' | 'low' | 'medium' | 'high' | 'critical' = 'safe'

  if (analysis.isDestructive && analysis.needsElevation) {
    risk = 'critical'
  } else if (analysis.isDestructive) {
    risk = 'high'
  } else if (analysis.needsElevation) {
    risk = 'medium'
  } else if (analysis.commands.some((c) => SYSTEM_COMMANDS.has(c))) {
    risk = 'medium'
  } else if (!analysis.commands.every((c) => SAFE_COMMANDS.has(c))) {
    risk = 'low'
  }

  return { risk, reasons }
}

/**
 * Extract all file paths that will be affected by a command
 */
export function getAffectedPaths(command: string): string[] {
  const analysis = analyzeBash(command)
  return [...analysis.files, ...analysis.directories]
}
