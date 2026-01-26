/**
 * Delta9 Tool Response Builder
 *
 * Standardized response formatting for tools to ensure consistent
 * JSON output structure across all Delta9 tools.
 */

import type { Delta9Error } from './errors.js'

// =============================================================================
// Types
// =============================================================================

/**
 * Base success response structure
 */
export interface ToolSuccessResponse {
  success: true
  [key: string]: unknown
}

/**
 * Base error response structure
 */
export interface ToolErrorResponse {
  success: false
  error: string
  code?: string
  suggestions?: string[]
}

/**
 * Union type for all tool responses
 */
export type ToolResponse = ToolSuccessResponse | ToolErrorResponse

/**
 * Options for error responses
 */
export interface ErrorResponseOptions {
  /** Error code for categorization */
  code?: string
  /** Suggestions for recovery */
  suggestions?: string[]
}

// =============================================================================
// Response Builders
// =============================================================================

/**
 * Create a success response
 *
 * @param data - Data to include in the response
 * @returns JSON string with success: true and data
 */
export function success<T extends object>(data: T): string {
  return JSON.stringify({ success: true, ...data })
}

/**
 * Create an error response
 *
 * @param message - Error message
 * @param opts - Optional code and suggestions
 * @returns JSON string with success: false and error info
 */
export function error(message: string, opts?: ErrorResponseOptions): string {
  const response: ToolErrorResponse = {
    success: false,
    error: message,
  }

  if (opts?.code) {
    response.code = opts.code
  }

  if (opts?.suggestions && opts.suggestions.length > 0) {
    response.suggestions = opts.suggestions
  }

  return JSON.stringify(response)
}

/**
 * Create an error response from a Delta9Error
 *
 * @param err - Delta9Error instance
 * @returns JSON string with success: false and error info
 */
export function fromDelta9Error(err: Delta9Error): string {
  return error(err.message, {
    code: err.code,
    suggestions: err.suggestions,
  })
}

// =============================================================================
// Type Guards
// =============================================================================

/**
 * Check if a response is a success response
 */
export function isSuccessResponse(response: unknown): response is ToolSuccessResponse {
  return (
    typeof response === 'object' &&
    response !== null &&
    'success' in response &&
    (response as ToolSuccessResponse).success === true
  )
}

/**
 * Check if a response is an error response
 */
export function isErrorResponse(response: unknown): response is ToolErrorResponse {
  return (
    typeof response === 'object' &&
    response !== null &&
    'success' in response &&
    (response as ToolErrorResponse).success === false &&
    'error' in response
  )
}

/**
 * Parse a JSON response string and determine type
 */
export function parseResponse(jsonString: string): ToolResponse | null {
  try {
    const parsed = JSON.parse(jsonString)
    if (isSuccessResponse(parsed) || isErrorResponse(parsed)) {
      return parsed
    }
    return null
  } catch {
    return null
  }
}
