/**
 * HTTP Error Classification Utilities
 * Shared error handling for LLM provider HTTP responses.
 */

import type { StreamError } from '@ava/core-v2/llm'

/**
 * Classify an HTTP status code into a StreamError type.
 */
export function classifyHttpError(status: number): StreamError['type'] {
  if (status === 401 || status === 403) return 'auth'
  if (status === 429) return 'rate_limit'
  if (status === 529) return 'server' // Anthropic overloaded
  if (status >= 500) return 'server'
  return 'unknown'
}

/**
 * Extract a meaningful error message from an HTTP error response body.
 */
export function extractErrorMessage(status: number, body: string, provider: string): string {
  try {
    const parsed = JSON.parse(body)
    if (parsed.error?.message) return `${provider} API error (${status}): ${parsed.error.message}`
    if (parsed.message) return `${provider} API error (${status}): ${parsed.message}`
  } catch {
    // Body not JSON
  }

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

  return `${provider} API error: ${messages[status] || `HTTP ${status}`}`
}

/**
 * Parse the Retry-After header value (seconds or HTTP date).
 * Returns seconds to wait, or undefined if unparseable.
 */
export function parseRetryAfter(header: string | null | undefined): number | undefined {
  if (!header) return undefined

  // Try numeric (seconds)
  const seconds = Number(header)
  if (!Number.isNaN(seconds) && Number.isFinite(seconds)) {
    return Math.max(0, Math.floor(seconds))
  }

  // Try HTTP date (e.g., "Mon, 02 Mar 2026 17:00:00 GMT")
  const date = Date.parse(header)
  if (!Number.isNaN(date)) {
    const deltaMs = date - Date.now()
    return Math.max(0, Math.ceil(deltaMs / 1000))
  }

  return undefined
}

/**
 * Parse retry delay from response headers.
 * Checks `retry-after-ms` first (milliseconds), then `retry-after` (seconds or HTTP date).
 * Returns milliseconds to wait, or undefined if no valid header found.
 */
export function parseRetryAfterMs(headers: Headers): number | undefined {
  // 1. Check retry-after-ms (milliseconds, used by some providers like Anthropic)
  const msHeader = headers.get('retry-after-ms')
  if (msHeader) {
    const ms = Number(msHeader)
    if (!Number.isNaN(ms) && Number.isFinite(ms)) {
      return Math.max(0, Math.floor(ms))
    }
  }

  // 2. Fall back to retry-after (seconds or HTTP date)
  const retryAfter = parseRetryAfter(headers.get('retry-after'))
  if (retryAfter !== undefined) {
    return retryAfter * 1000
  }

  return undefined
}

/**
 * Build a StreamError from an HTTP response.
 */
export function buildHttpError(status: number, body: string, provider: string): StreamError {
  return {
    type: classifyHttpError(status),
    message: extractErrorMessage(status, body, provider),
    status,
    retryAfter: undefined,
  }
}
