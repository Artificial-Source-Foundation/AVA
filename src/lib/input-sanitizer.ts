/**
 * Delta9 Input Sanitization (D-5)
 *
 * Sanitizes and validates all input to prevent:
 * - Command injection
 * - Path traversal
 * - Malicious patterns
 * - Invalid data formats
 */

// Input sanitizer - logger available for debugging if needed
// import { getNamedLogger } from './logger.js'
// const log = getNamedLogger('input-sanitizer')

// =============================================================================
// Types
// =============================================================================

/** Sanitization result */
export interface SanitizeResult<T> {
  /** Whether input is valid */
  valid: boolean
  /** Sanitized value */
  value: T
  /** Original value */
  original: unknown
  /** Issues found */
  issues: string[]
  /** Whether input was modified during sanitization */
  modified: boolean
}

/** Sanitizer configuration */
export interface SanitizerConfig {
  /** Allow path traversal characters (../) */
  allowPathTraversal?: boolean
  /** Allow shell metacharacters */
  allowShellMeta?: boolean
  /** Maximum string length */
  maxLength?: number
  /** Maximum array length */
  maxArrayLength?: number
  /** Maximum object depth */
  maxObjectDepth?: number
  /** Custom blocked patterns */
  blockedPatterns?: RegExp[]
  /** Custom allowed patterns */
  allowedPatterns?: RegExp[]
}

/** Default sanitizer configuration */
const DEFAULT_CONFIG: Required<SanitizerConfig> = {
  allowPathTraversal: false,
  allowShellMeta: false,
  maxLength: 100000,
  maxArrayLength: 1000,
  maxObjectDepth: 10,
  blockedPatterns: [],
  allowedPatterns: [],
}

// =============================================================================
// Dangerous Patterns
// =============================================================================

/** Shell metacharacters that could enable command injection */
const SHELL_METACHARACTERS = /[;&|`$(){}[\]<>!\\]/g

/** Path traversal patterns */
const PATH_TRAVERSAL = /\.\.[/\\]/g

/** Null byte injection */
const NULL_BYTE = /\x00/g

/** Control characters (except newline and tab) */
const CONTROL_CHARS = /[\x00-\x08\x0B\x0C\x0E-\x1F\x7F]/g

/** Common injection patterns */
const INJECTION_PATTERNS = [
  /;\s*rm\s+-rf/i,           // rm -rf
  /;\s*wget\s+/i,            // wget
  /;\s*curl\s+/i,            // curl
  /'\s*OR\s+'1'\s*=\s*'1/i,  // SQL injection
  /<script[^>]*>/i,          // XSS script tags
  /javascript:/i,            // JavaScript protocol
  /data:text\/html/i,        // Data URI injection
]

// =============================================================================
// Input Sanitizer Class
// =============================================================================

export class InputSanitizer {
  private config: Required<SanitizerConfig>

  constructor(config: SanitizerConfig = {}) {
    this.config = { ...DEFAULT_CONFIG, ...config }
  }

  // ===========================================================================
  // String Sanitization
  // ===========================================================================

  /**
   * Sanitize a string input
   */
  sanitizeString(input: unknown): SanitizeResult<string> {
    const issues: string[] = []
    let modified = false

    // Type check
    if (typeof input !== 'string') {
      if (input === null || input === undefined) {
        return {
          valid: true,
          value: '',
          original: input,
          issues: [],
          modified: true,
        }
      }
      return {
        valid: false,
        value: String(input),
        original: input,
        issues: ['Expected string, got ' + typeof input],
        modified: true,
      }
    }

    let value = input

    // Length check
    if (value.length > this.config.maxLength) {
      issues.push(`String exceeds max length (${value.length} > ${this.config.maxLength})`)
      value = value.slice(0, this.config.maxLength)
      modified = true
    }

    // Remove null bytes
    if (NULL_BYTE.test(value)) {
      issues.push('Null bytes detected and removed')
      value = value.replace(NULL_BYTE, '')
      modified = true
    }

    // Remove control characters
    if (CONTROL_CHARS.test(value)) {
      issues.push('Control characters detected and removed')
      value = value.replace(CONTROL_CHARS, '')
      modified = true
    }

    // Check injection patterns
    for (const pattern of INJECTION_PATTERNS) {
      if (pattern.test(value)) {
        issues.push(`Potential injection pattern detected: ${pattern.source}`)
        // Don't modify, just flag
      }
    }

    // Check custom blocked patterns
    for (const pattern of this.config.blockedPatterns) {
      if (pattern.test(value)) {
        issues.push(`Blocked pattern matched: ${pattern.source}`)
      }
    }

    return {
      valid: issues.filter((i) => !i.startsWith('Potential') && !i.startsWith('Blocked')).length === 0,
      value,
      original: input,
      issues,
      modified,
    }
  }

  /**
   * Sanitize a file path
   */
  sanitizePath(input: unknown): SanitizeResult<string> {
    const stringResult = this.sanitizeString(input)
    if (!stringResult.valid) return stringResult

    const issues = [...stringResult.issues]
    let value = stringResult.value
    let modified = stringResult.modified

    // Check path traversal
    if (!this.config.allowPathTraversal && PATH_TRAVERSAL.test(value)) {
      issues.push('Path traversal detected')
      value = value.replace(PATH_TRAVERSAL, '')
      modified = true
    }

    // Normalize path separators
    if (value.includes('\\')) {
      value = value.replace(/\\/g, '/')
      modified = true
    }

    // Remove double slashes
    if (value.includes('//')) {
      value = value.replace(/\/+/g, '/')
      modified = true
    }

    return {
      valid: issues.filter((i) => i === 'Path traversal detected').length === 0,
      value,
      original: input,
      issues,
      modified,
    }
  }

  /**
   * Sanitize a shell command argument
   */
  sanitizeShellArg(input: unknown): SanitizeResult<string> {
    const stringResult = this.sanitizeString(input)
    if (!stringResult.valid) return stringResult

    const issues = [...stringResult.issues]
    let value = stringResult.value
    let modified = stringResult.modified

    // Check shell metacharacters
    if (!this.config.allowShellMeta && SHELL_METACHARACTERS.test(value)) {
      issues.push('Shell metacharacters detected')
      // Escape instead of remove
      value = value.replace(SHELL_METACHARACTERS, '\\$&')
      modified = true
    }

    return {
      valid: true,
      value,
      original: input,
      issues,
      modified,
    }
  }

  // ===========================================================================
  // Number Sanitization
  // ===========================================================================

  /**
   * Sanitize a number input
   */
  sanitizeNumber(
    input: unknown,
    options: { min?: number; max?: number; integer?: boolean } = {}
  ): SanitizeResult<number> {
    const issues: string[] = []
    let modified = false

    // Parse number
    let value: number
    if (typeof input === 'number') {
      value = input
    } else if (typeof input === 'string') {
      value = parseFloat(input)
      modified = true
    } else {
      return {
        valid: false,
        value: 0,
        original: input,
        issues: ['Expected number, got ' + typeof input],
        modified: true,
      }
    }

    // NaN check
    if (isNaN(value)) {
      return {
        valid: false,
        value: 0,
        original: input,
        issues: ['Value is NaN'],
        modified: true,
      }
    }

    // Infinity check
    if (!isFinite(value)) {
      issues.push('Value is Infinity')
      value = value > 0 ? Number.MAX_SAFE_INTEGER : Number.MIN_SAFE_INTEGER
      modified = true
    }

    // Integer check
    if (options.integer && !Number.isInteger(value)) {
      value = Math.round(value)
      issues.push('Non-integer rounded')
      modified = true
    }

    // Range checks
    if (options.min !== undefined && value < options.min) {
      issues.push(`Value below minimum (${value} < ${options.min})`)
      value = options.min
      modified = true
    }

    if (options.max !== undefined && value > options.max) {
      issues.push(`Value above maximum (${value} > ${options.max})`)
      value = options.max
      modified = true
    }

    return {
      valid: true,
      value,
      original: input,
      issues,
      modified,
    }
  }

  // ===========================================================================
  // Array Sanitization
  // ===========================================================================

  /**
   * Sanitize an array input
   */
  sanitizeArray<T>(
    input: unknown,
    itemSanitizer: (item: unknown) => SanitizeResult<T>
  ): SanitizeResult<T[]> {
    const issues: string[] = []
    let modified = false

    // Type check
    if (!Array.isArray(input)) {
      return {
        valid: false,
        value: [],
        original: input,
        issues: ['Expected array, got ' + typeof input],
        modified: true,
      }
    }

    let items = input

    // Length check
    if (items.length > this.config.maxArrayLength) {
      issues.push(`Array exceeds max length (${items.length} > ${this.config.maxArrayLength})`)
      items = items.slice(0, this.config.maxArrayLength)
      modified = true
    }

    // Sanitize each item
    const sanitizedItems: T[] = []
    for (let i = 0; i < items.length; i++) {
      const result = itemSanitizer(items[i])
      if (!result.valid) {
        issues.push(`Item ${i}: ${result.issues.join(', ')}`)
      }
      if (result.modified) {
        modified = true
      }
      sanitizedItems.push(result.value)
    }

    return {
      valid: issues.length === 0,
      value: sanitizedItems,
      original: input,
      issues,
      modified,
    }
  }

  // ===========================================================================
  // Object Sanitization
  // ===========================================================================

  /**
   * Sanitize an object input
   */
  sanitizeObject(
    input: unknown,
    schema: Record<string, (v: unknown) => SanitizeResult<unknown>>,
    depth = 0
  ): SanitizeResult<Record<string, unknown>> {
    const issues: string[] = []
    let modified = false

    // Depth check
    if (depth > this.config.maxObjectDepth) {
      return {
        valid: false,
        value: {},
        original: input,
        issues: ['Object depth exceeds maximum'],
        modified: true,
      }
    }

    // Type check
    if (input === null || typeof input !== 'object' || Array.isArray(input)) {
      return {
        valid: false,
        value: {},
        original: input,
        issues: ['Expected object, got ' + (input === null ? 'null' : typeof input)],
        modified: true,
      }
    }

    const obj = input as Record<string, unknown>
    const sanitized: Record<string, unknown> = {}

    // Sanitize each field according to schema
    for (const [key, sanitizer] of Object.entries(schema)) {
      if (key in obj) {
        const result = sanitizer(obj[key])
        if (!result.valid) {
          issues.push(`${key}: ${result.issues.join(', ')}`)
        }
        if (result.modified) {
          modified = true
        }
        sanitized[key] = result.value
      }
    }

    // Check for unexpected fields
    for (const key of Object.keys(obj)) {
      if (!(key in schema)) {
        issues.push(`Unexpected field: ${key}`)
      }
    }

    return {
      valid: issues.filter((i) => !i.startsWith('Unexpected')).length === 0,
      value: sanitized,
      original: input,
      issues,
      modified,
    }
  }
}

// =============================================================================
// Factory Functions
// =============================================================================

/** Default sanitizer instance */
let defaultSanitizer: InputSanitizer | null = null

/**
 * Get or create the default input sanitizer
 */
export function getInputSanitizer(config?: SanitizerConfig): InputSanitizer {
  if (!defaultSanitizer) {
    defaultSanitizer = new InputSanitizer(config)
  }
  return defaultSanitizer
}

/**
 * Reset the default sanitizer (for testing)
 */
export function resetInputSanitizer(): void {
  defaultSanitizer = null
}

/**
 * Create a new input sanitizer
 */
export function createInputSanitizer(config?: SanitizerConfig): InputSanitizer {
  return new InputSanitizer(config)
}

// =============================================================================
// Convenience Functions
// =============================================================================

/**
 * Sanitize a string (convenience function)
 */
export function sanitizeString(input: unknown): SanitizeResult<string> {
  return getInputSanitizer().sanitizeString(input)
}

/**
 * Sanitize a file path (convenience function)
 */
export function sanitizePath(input: unknown): SanitizeResult<string> {
  return getInputSanitizer().sanitizePath(input)
}

/**
 * Sanitize a shell argument (convenience function)
 */
export function sanitizeShellArg(input: unknown): SanitizeResult<string> {
  return getInputSanitizer().sanitizeShellArg(input)
}

/**
 * Sanitize a number (convenience function)
 */
export function sanitizeNumber(
  input: unknown,
  options?: { min?: number; max?: number; integer?: boolean }
): SanitizeResult<number> {
  return getInputSanitizer().sanitizeNumber(input, options)
}

/**
 * Quick validation: check if string is safe
 */
export function isStringSafe(input: string): boolean {
  const result = sanitizeString(input)
  return result.valid && !result.modified
}

/**
 * Quick validation: check if path is safe
 */
export function isPathSafe(input: string): boolean {
  const result = sanitizePath(input)
  return result.valid && !result.modified
}

/**
 * Quick validation: check if shell arg is safe
 */
export function isShellArgSafe(input: string): boolean {
  const result = sanitizeShellArg(input)
  return result.valid && !result.modified
}
