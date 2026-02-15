/**
 * Retry Utilities for LLM Providers
 * Exponential backoff with jitter for transient failures
 */

import type { StreamError } from '../../types/llm.js'

// ============================================================================
// Types
// ============================================================================

export interface RetryConfig {
  /** Maximum number of retry attempts (default: 3) */
  maxRetries?: number
  /** Base delay in ms for exponential backoff (default: 1000) */
  baseDelayMs?: number
  /** Maximum delay in ms (default: 60000) */
  maxDelayMs?: number
}

const DEFAULT_RETRY_CONFIG: Required<RetryConfig> = {
  maxRetries: 3,
  baseDelayMs: 1000,
  maxDelayMs: 60_000,
}

// ============================================================================
// Core Functions
// ============================================================================

/**
 * Determine if an error is retryable based on its type.
 * Rate limit and server errors are retryable; auth and unknown are not.
 */
export function shouldRetry(
  errorType: StreamError['type'],
  attempt: number,
  maxRetries: number
): boolean {
  if (attempt >= maxRetries) return false

  switch (errorType) {
    case 'rate_limit':
    case 'server':
    case 'network':
      return true
    case 'auth':
    case 'unknown':
    case 'api':
      return false
    default:
      return false
  }
}

/**
 * Calculate delay before next retry attempt.
 * Uses exponential backoff with jitter, honoring Retry-After header if present.
 *
 * @param attempt - Current attempt number (0-based)
 * @param retryAfterSeconds - Optional Retry-After header value in seconds
 * @param config - Retry configuration
 * @returns Delay in milliseconds
 */
export function calculateDelay(
  attempt: number,
  retryAfterSeconds?: number,
  config?: RetryConfig
): number {
  const { baseDelayMs, maxDelayMs } = { ...DEFAULT_RETRY_CONFIG, ...config }

  // Honor Retry-After header if present
  if (retryAfterSeconds !== undefined && retryAfterSeconds > 0) {
    return Math.min(retryAfterSeconds * 1000, maxDelayMs)
  }

  // Exponential backoff: base * 2^attempt
  const exponential = baseDelayMs * 2 ** attempt

  // Add jitter: random value between 0 and 50% of the exponential delay
  const jitter = Math.random() * exponential * 0.5

  return Math.min(exponential + jitter, maxDelayMs)
}

/**
 * Sleep for a specified duration
 */
export function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

/**
 * Execute an async function with retry logic.
 * Designed for non-streaming operations (e.g., token exchange, metadata calls).
 *
 * @param fn - Async function to retry
 * @param config - Retry configuration
 * @returns Result of the function
 */
export async function withRetry<T>(fn: () => Promise<T>, config?: RetryConfig): Promise<T> {
  const { maxRetries, baseDelayMs, maxDelayMs } = { ...DEFAULT_RETRY_CONFIG, ...config }

  let lastError: Error | undefined

  for (let attempt = 0; attempt <= maxRetries; attempt++) {
    try {
      return await fn()
    } catch (err) {
      lastError = err instanceof Error ? err : new Error(String(err))

      if (attempt >= maxRetries) break

      const delay = calculateDelay(attempt, undefined, { baseDelayMs, maxDelayMs })
      await sleep(delay)
    }
  }

  throw lastError!
}
