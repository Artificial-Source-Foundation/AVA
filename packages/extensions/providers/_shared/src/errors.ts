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
 * Parse the Retry-After header value.
 */
export function parseRetryAfter(header: string | null | undefined): number | undefined {
  if (!header) return undefined
  const seconds = parseInt(header, 10)
  return Number.isNaN(seconds) ? undefined : seconds
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
