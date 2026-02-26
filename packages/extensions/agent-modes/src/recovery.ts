/**
 * Recovery strategies for agent failures.
 *
 * Classifies errors and suggests recovery approaches.
 */

export type ErrorCategory =
  | 'permission'
  | 'not_found'
  | 'timeout'
  | 'network'
  | 'validation'
  | 'syntax'
  | 'resource'
  | 'rate_limit'
  | 'unknown'

export type RecoveryStrategy = 'retry' | 'alternate' | 'decompose' | 'skip' | 'abort'

const ERROR_PATTERNS: Array<{ pattern: RegExp; category: ErrorCategory }> = [
  { pattern: /EACCES|permission denied|access denied/i, category: 'permission' },
  { pattern: /ENOENT|not found|no such file/i, category: 'not_found' },
  { pattern: /timeout|timed out|ETIMEDOUT/i, category: 'timeout' },
  { pattern: /ECONNRESET|ENOTFOUND|network|fetch failed/i, category: 'network' },
  { pattern: /syntax error|parse error|SyntaxError/i, category: 'syntax' },
  { pattern: /invalid|malformed|unexpected token/i, category: 'validation' },
  { pattern: /out of memory|ENOMEM|disk full|ENOSPC/i, category: 'resource' },
  { pattern: /429|rate limit|too many requests/i, category: 'rate_limit' },
]

const STRATEGY_MAP: Record<ErrorCategory, RecoveryStrategy> = {
  permission: 'alternate',
  not_found: 'decompose',
  timeout: 'retry',
  network: 'retry',
  validation: 'alternate',
  syntax: 'abort',
  resource: 'abort',
  rate_limit: 'retry',
  unknown: 'skip',
}

export function classifyError(error: string): ErrorCategory {
  for (const { pattern, category } of ERROR_PATTERNS) {
    if (pattern.test(error)) return category
  }
  return 'unknown'
}

export function suggestStrategy(error: string): RecoveryStrategy {
  const category = classifyError(error)
  return STRATEGY_MAP[category]
}

export function isRetryable(error: string): boolean {
  const strategy = suggestStrategy(error)
  return strategy === 'retry'
}

export interface RetryOptions {
  maxAttempts: number
  initialDelayMs: number
  maxDelayMs: number
  jitterFactor: number
}

const DEFAULT_RETRY: RetryOptions = {
  maxAttempts: 3,
  initialDelayMs: 1000,
  maxDelayMs: 30_000,
  jitterFactor: 0.3,
}

export function calculateBackoff(attempt: number, options: Partial<RetryOptions> = {}): number {
  const opts = { ...DEFAULT_RETRY, ...options }
  const delay = Math.min(opts.initialDelayMs * 2 ** (attempt - 1), opts.maxDelayMs)
  const jitter = delay * opts.jitterFactor * (Math.random() * 2 - 1)
  return Math.max(0, delay + jitter)
}

export async function retryWithBackoff<T>(
  fn: () => Promise<T>,
  options: Partial<RetryOptions> = {}
): Promise<T> {
  const opts = { ...DEFAULT_RETRY, ...options }
  let lastError: unknown

  for (let attempt = 1; attempt <= opts.maxAttempts; attempt++) {
    try {
      return await fn()
    } catch (err) {
      lastError = err
      if (attempt < opts.maxAttempts) {
        const delay = calculateBackoff(attempt, opts)
        await new Promise((resolve) => setTimeout(resolve, delay))
      }
    }
  }

  throw lastError
}
