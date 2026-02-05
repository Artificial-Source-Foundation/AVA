/**
 * Command Validator
 * Validates shell commands for security before execution
 *
 * Security Features:
 * - Validates EACH segment of chained/piped commands
 * - Detects dangerous characters outside quotes
 * - Blocks Unicode injection attacks
 * - Supports allow/deny glob patterns
 * - Respects redirect permissions
 *
 * Based on Cline's CommandPermissionController pattern
 */

import {
  type CommandSegment,
  type DangerousCharResult,
  detectDangerousCharacters,
  detectRedirects,
  extractSubshells,
  parseCommandSegments,
} from './quote-parser.js'

// ============================================================================
// Types
// ============================================================================

/**
 * Configuration for command permissions
 * Can be set via ESTELA_COMMAND_PERMISSIONS environment variable
 */
export interface CommandPermissionConfig {
  /** Glob patterns for allowed commands */
  allow?: string[]
  /** Glob patterns for denied commands (checked first, takes precedence) */
  deny?: string[]
  /** Whether to allow shell redirects (>, >>, <, etc.) */
  allowRedirects?: boolean
}

/**
 * Result of command validation
 */
export interface CommandValidationResult {
  /** Whether the command is allowed */
  allowed: boolean
  /** Pattern that matched (for allow/deny) */
  matchedPattern?: string
  /** Reason for the decision */
  reason: CommandValidationReason
  /** Detected dangerous operator (if any) */
  detectedOperator?: string
  /** Which segment failed validation (for chained commands) */
  failedSegment?: string
  /** Index of failed segment */
  failedSegmentIndex?: number
  /** All parsed segments (for debugging) */
  segments?: CommandSegment[]
  /** Detected redirects */
  redirects?: string[]
  /** Detected subshells */
  subshells?: string[]
}

/**
 * Validation failure reasons
 */
export type CommandValidationReason =
  | 'no_config' // No config set, allow by default
  | 'allowed' // Matched allow pattern
  | 'denied' // Matched deny pattern
  | 'no_match_deny_default' // Allow rules exist but no match
  | 'dangerous_char_detected' // Backticks, newlines, etc.
  | 'redirect_detected' // Redirects not allowed
  | 'segment_denied' // A segment in chain matched deny
  | 'segment_no_match' // A segment in chain didn't match allow
  | 'subshell_denied' // Subshell content denied
  | 'empty_command' // Empty or whitespace-only command

// ============================================================================
// Command Permission Controller
// ============================================================================

export class CommandValidator {
  private config: CommandPermissionConfig | null

  constructor(config?: CommandPermissionConfig) {
    this.config = config ?? this.parseConfigFromEnv()
  }

  /**
   * Parse config from ESTELA_COMMAND_PERMISSIONS environment variable
   */
  private parseConfigFromEnv(): CommandPermissionConfig | null {
    const envValue = process.env.ESTELA_COMMAND_PERMISSIONS
    if (!envValue) return null

    try {
      return JSON.parse(envValue) as CommandPermissionConfig
    } catch {
      console.warn('Invalid ESTELA_COMMAND_PERMISSIONS JSON, ignoring')
      return null
    }
  }

  /**
   * Update the permission configuration
   */
  setConfig(config: CommandPermissionConfig | null): void {
    this.config = config
  }

  /**
   * Get current configuration
   */
  getConfig(): CommandPermissionConfig | null {
    return this.config
  }

  /**
   * Validate a command for execution
   *
   * Security flow:
   * 1. Check for dangerous characters (backticks, newlines, unicode)
   * 2. Parse into segments (split by |, &&, ||, ;)
   * 3. Check for redirects
   * 4. Extract and validate subshells
   * 5. Validate EACH segment against allow/deny rules
   */
  validate(command: string): CommandValidationResult {
    // Handle empty commands
    const trimmed = command.trim()
    if (!trimmed) {
      return {
        allowed: false,
        reason: 'empty_command',
      }
    }

    // Step 1: Check for dangerous characters
    const dangerCheck = detectDangerousCharacters(trimmed)
    if (dangerCheck.found) {
      return {
        allowed: false,
        reason: 'dangerous_char_detected',
        detectedOperator: dangerCheck.character,
        failedSegment: trimmed,
      }
    }

    // Step 2: Parse command into segments
    const segments = parseCommandSegments(trimmed)

    // Step 3: Check for redirects
    const redirects = detectRedirects(trimmed)
    if (redirects.length > 0 && this.config && !this.config.allowRedirects) {
      return {
        allowed: false,
        reason: 'redirect_detected',
        detectedOperator: redirects[0],
        segments,
        redirects,
      }
    }

    // Step 4: Extract and validate subshells
    const subshells = extractSubshells(trimmed)
    for (const subshell of subshells) {
      const subResult = this.validate(subshell)
      if (!subResult.allowed) {
        return {
          allowed: false,
          reason: 'subshell_denied',
          failedSegment: subshell,
          segments,
          subshells,
        }
      }
    }

    // No config = allow all (backward compatibility)
    if (!this.config) {
      return {
        allowed: true,
        reason: 'no_config',
        segments,
        redirects,
        subshells,
      }
    }

    // Step 5: Validate EACH segment
    for (let i = 0; i < segments.length; i++) {
      const segment = segments[i]
      const segmentResult = this.validateSegment(segment.command)

      if (!segmentResult.allowed) {
        return {
          allowed: false,
          reason: segmentResult.reason,
          matchedPattern: segmentResult.matchedPattern,
          failedSegment: segment.command,
          failedSegmentIndex: i,
          segments,
          redirects,
          subshells,
        }
      }
    }

    // All segments passed
    return {
      allowed: true,
      reason: 'allowed',
      segments,
      redirects,
      subshells,
    }
  }

  /**
   * Validate a single command segment
   */
  private validateSegment(segment: string): CommandValidationResult {
    const trimmed = segment.trim()

    if (!this.config) {
      return { allowed: true, reason: 'no_config' }
    }

    // Check deny patterns FIRST (deny takes precedence)
    if (this.config.deny) {
      for (const pattern of this.config.deny) {
        if (this.matchesPattern(trimmed, pattern)) {
          return {
            allowed: false,
            reason: 'segment_denied',
            matchedPattern: pattern,
          }
        }
      }
    }

    // Check allow patterns
    if (this.config.allow && this.config.allow.length > 0) {
      for (const pattern of this.config.allow) {
        if (this.matchesPattern(trimmed, pattern)) {
          return {
            allowed: true,
            reason: 'allowed',
            matchedPattern: pattern,
          }
        }
      }

      // Allow list exists but no match = deny
      return {
        allowed: false,
        reason: 'segment_no_match',
      }
    }

    // No allow list defined = allow by default
    return { allowed: true, reason: 'no_config' }
  }

  /**
   * Check if a command matches a glob pattern
   *
   * Pattern syntax:
   * - * matches any characters (including /)
   * - ? matches exactly one character
   * - Patterns match from the start
   */
  private matchesPattern(command: string, pattern: string): boolean {
    // Convert glob pattern to regex
    const regexStr = pattern
      .replace(/[.+^${}()|[\]\\]/g, '\\$&') // Escape special regex chars
      .replace(/\*\*/g, '<<<DOUBLE_STAR>>>') // Temporarily replace **
      .replace(/\*/g, '.*') // * matches anything
      .replace(/<<<DOUBLE_STAR>>>/g, '.*') // ** also matches anything
      .replace(/\?/g, '.') // ? matches single char

    // Use 's' flag so . matches newlines
    const regex = new RegExp(`^${regexStr}$`, 's')
    return regex.test(command)
  }

  /**
   * Quick check if a command contains any dangerous patterns
   * Useful for early rejection before full validation
   */
  quickDangerCheck(command: string): DangerousCharResult {
    return detectDangerousCharacters(command)
  }

  /**
   * Parse a command into segments without validating
   * Useful for debugging and inspection
   */
  parseSegments(command: string): CommandSegment[] {
    return parseCommandSegments(command)
  }
}

// ============================================================================
// Singleton Instance
// ============================================================================

/** Global command validator instance */
let globalValidator: CommandValidator | null = null

/**
 * Get the global command validator instance
 */
export function getCommandValidator(): CommandValidator {
  if (!globalValidator) {
    globalValidator = new CommandValidator()
  }
  return globalValidator
}

/**
 * Set global command permission config
 */
export function setCommandPermissions(config: CommandPermissionConfig | null): void {
  getCommandValidator().setConfig(config)
}

/**
 * Validate a command using the global validator
 */
export function validateCommand(command: string): CommandValidationResult {
  return getCommandValidator().validate(command)
}

/**
 * Quick danger check using the global validator
 */
export function quickDangerCheck(command: string): DangerousCharResult {
  return getCommandValidator().quickDangerCheck(command)
}

// ============================================================================
// Preset Configurations
// ============================================================================

/**
 * Development workflow preset
 * Allows common dev commands, blocks destructive ones
 */
export const DEV_WORKFLOW_CONFIG: CommandPermissionConfig = {
  allow: [
    // Package managers
    'npm *',
    'pnpm *',
    'yarn *',
    'bun *',
    // Version control
    'git *',
    // Node/runtime
    'node *',
    'npx *',
    'deno *',
    // File operations (read)
    'cat *',
    'ls *',
    'head *',
    'tail *',
    'less *',
    'more *',
    // Searching
    'grep *',
    'rg *',
    'find *',
    'fd *',
    // Directory ops
    'cd *',
    'pwd',
    'mkdir *',
    'touch *',
    // Common tools
    'echo *',
    'which *',
    'type *',
    'env',
    'printenv',
  ],
  deny: [
    // Destructive
    'rm -rf *',
    'rm -r *',
    'rmdir *',
    // Privilege escalation
    'sudo *',
    'su *',
    'doas *',
    // System modification
    'chmod 777 *',
    'chown *',
    // Network attacks
    'curl * | bash*',
    'wget * | bash*',
    'curl * | sh*',
    'wget * | sh*',
  ],
  allowRedirects: true,
}

/**
 * Read-only preset
 * Only allows commands that don't modify anything
 */
export const READ_ONLY_CONFIG: CommandPermissionConfig = {
  allow: [
    'cat *',
    'ls *',
    'head *',
    'tail *',
    'less *',
    'more *',
    'grep *',
    'rg *',
    'find *',
    'fd *',
    'file *',
    'wc *',
    'sort *',
    'uniq *',
    'diff *',
    'git status',
    'git log*',
    'git diff*',
    'git show*',
    'git branch*',
    'pwd',
    'whoami',
    'date',
    'echo *',
  ],
  allowRedirects: false,
}

/**
 * Strict preset
 * Only explicit commands allowed, no glob patterns
 * Note: No deny list needed - allow-only means unmatched commands are rejected
 */
export const STRICT_CONFIG: CommandPermissionConfig = {
  allow: ['pwd', 'whoami', 'date', 'ls', 'git status'],
  // No deny needed - when allow list exists but doesn't match, command is rejected
  allowRedirects: false,
}
