/**
 * Zod Validation Utilities
 * Helper functions for Zod schema handling in tools
 */

import type { ZodError, ZodIssue } from 'zod'

// ============================================================================
// Error Formatting
// ============================================================================

/**
 * Format a single Zod issue into a readable string
 */
function formatIssue(issue: ZodIssue): string {
  const path = issue.path.length > 0 ? issue.path.join('.') : 'input'
  return `${issue.path.length > 0 ? path : 'input'}: ${issue.message}`
}

/**
 * Format Zod validation errors into a readable string
 */
export function formatZodError<T>(error: ZodError<T>): string {
  const issues = error.issues.map(formatIssue)

  if (issues.length === 1) {
    return `Validation error: ${issues[0]}`
  }

  return `Validation errors:\n${issues.map((issue: string) => `  - ${issue}`).join('\n')}`
}

// ============================================================================
// Schema Utilities
// ============================================================================

/**
 * Check if a value is a valid Zod schema
 */
export function isZodSchema(value: unknown): boolean {
  return (
    value !== null &&
    typeof value === 'object' &&
    '_def' in value &&
    'safeParse' in value &&
    typeof (value as { safeParse: unknown }).safeParse === 'function'
  )
}

/**
 * Common schema patterns for tool inputs
 */
export const commonSchemas = {
  /**
   * File path - non-empty string
   */
  filePath: {
    type: 'string' as const,
    minLength: 1,
    description: 'File path (absolute or relative to working directory)',
  },

  /**
   * Directory path - non-empty string
   */
  directoryPath: {
    type: 'string' as const,
    minLength: 1,
    description: 'Directory path (absolute or relative to working directory)',
  },

  /**
   * Glob pattern
   */
  globPattern: {
    type: 'string' as const,
    minLength: 1,
    description: 'Glob pattern (e.g., "**/*.ts", "src/{a,b}/*.js")',
  },

  /**
   * Regex pattern
   */
  regexPattern: {
    type: 'string' as const,
    minLength: 1,
    description: 'Regular expression pattern',
  },

  /**
   * File content
   */
  content: {
    type: 'string' as const,
    description: 'File content',
  },

  /**
   * Line number (1-indexed)
   */
  lineNumber: {
    type: 'number' as const,
    minimum: 1,
    description: 'Line number (1-indexed)',
  },

  /**
   * Positive integer
   */
  positiveInt: {
    type: 'number' as const,
    minimum: 1,
    description: 'Positive integer',
  },
} as const
