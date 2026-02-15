/**
 * HTTP Error Classification Utilities
 * Shared error handling for LLM provider HTTP responses
 */

import type { StreamError } from '../../types/llm.js'

/**
 * Classify an HTTP status code into a StreamError type
 */
export function classifyHttpError(status: number): StreamError['type'] {
  if (status === 401 || status === 403) return 'auth'
  if (status === 429) return 'rate_limit'
  if (status === 529) return 'server' // Anthropic overloaded
  if (status >= 500) return 'server'
  return 'unknown'
}

/**
 * Extract a meaningful error message from an HTTP error response body
 *
 * @param status - HTTP status code
 * @param body - Raw response body text
 * @param provider - Provider name for context in messages
 */
export function extractErrorMessage(status: number, body: string, provider: string): string {
  // Try to parse structured error from body
  try {
    const parsed = JSON.parse(body)
    // OpenAI-compatible format: { error: { message: "..." } }
    if (parsed.error?.message) return `${provider} API error (${status}): ${parsed.error.message}`
    // Alternative: { message: "..." }
    if (parsed.message) return `${provider} API error (${status}): ${parsed.message}`
  } catch {
    // Body not JSON — use as-is if short
  }

  // Fallback to status-based messages
  const messages: Record<number, string> = {
    400: 'Invalid request',
    401: 'Invalid API key',
    403: 'Access forbidden',
    404: 'Model not found',
    429: 'Rate limit exceeded',
    500: 'Server error',
    502: 'Bad gateway',
    503: 'Service unavailable',
    529: 'API overloaded - please retry',
  }

  const msg = messages[status] || `HTTP ${status}`
  return `${provider} API error: ${msg}`
}

/**
 * Parse the Retry-After header value
 *
 * @param header - Raw Retry-After header value (seconds or HTTP date)
 * @returns Number of seconds to wait, or undefined if not parseable
 */
export function parseRetryAfter(header: string | null | undefined): number | undefined {
  if (!header) return undefined
  const seconds = parseInt(header, 10)
  return Number.isNaN(seconds) ? undefined : seconds
}

/**
 * Build a StreamError from an HTTP response
 */
export function buildHttpError(status: number, body: string, provider: string): StreamError {
  return {
    type: classifyHttpError(status),
    message: extractErrorMessage(status, body, provider),
    status,
    retryAfter: undefined, // Caller should set from response headers
  }
}
