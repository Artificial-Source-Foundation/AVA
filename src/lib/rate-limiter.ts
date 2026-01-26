/**
 * Delta9 Rate Limiter
 *
 * Handles API rate limits with:
 * - Exponential backoff with jitter
 * - Request queuing
 * - Fallback model selection
 */

// =============================================================================
// Types
// =============================================================================

export interface RateLimitConfig {
  /** Maximum retry attempts */
  maxRetries: number
  /** Base delay in ms for exponential backoff */
  baseDelayMs: number
  /** Maximum delay in ms */
  maxDelayMs: number
  /** Jitter factor (0-1) */
  jitterFactor: number
}

export interface RateLimitError {
  /** HTTP status code (usually 429) */
  status: number
  /** Error message */
  message: string
  /** Retry-after header value (seconds) */
  retryAfter?: number
  /** Provider name */
  provider?: string
}

export interface RetryResult<T> {
  /** Whether the operation succeeded */
  success: boolean
  /** Result if successful */
  result?: T
  /** Error if failed */
  error?: Error
  /** Number of attempts made */
  attempts: number
  /** Total delay incurred (ms) */
  totalDelay: number
  /** Whether fallback was used */
  usedFallback: boolean
  /** Fallback model used (if any) */
  fallbackModel?: string
}

// =============================================================================
// Constants
// =============================================================================

/** Default rate limit configuration */
const DEFAULT_CONFIG: RateLimitConfig = {
  maxRetries: 3,
  baseDelayMs: 1000,
  maxDelayMs: 60000,
  jitterFactor: 0.2,
}

/** Fallback models by provider */
export const FALLBACK_MODELS: Record<string, string[]> = {
  // Anthropic fallbacks
  'anthropic/claude-opus-4': ['anthropic/claude-sonnet-4-5', 'openai/gpt-4o'],
  'anthropic/claude-sonnet-4-5': ['anthropic/claude-haiku-4', 'openai/gpt-4o-mini'],
  'anthropic/claude-haiku-4': ['openai/gpt-4o-mini', 'google/gemini-2.0-flash'],

  // OpenAI fallbacks
  'openai/gpt-4o': ['anthropic/claude-sonnet-4-5', 'google/gemini-2.0-pro'],
  'openai/gpt-4o-mini': ['anthropic/claude-haiku-4', 'google/gemini-2.0-flash'],

  // Google fallbacks
  'google/gemini-2.0-pro': ['openai/gpt-4o', 'anthropic/claude-sonnet-4-5'],
  'google/gemini-2.0-flash': ['openai/gpt-4o-mini', 'anthropic/claude-haiku-4'],

  // DeepSeek fallbacks
  'deepseek/deepseek-chat': ['anthropic/claude-haiku-4', 'google/gemini-2.0-flash'],
}

// =============================================================================
// Rate Limiter
// =============================================================================

export class RateLimiter {
  private config: RateLimitConfig
  private cooldowns: Map<string, number> = new Map()

  constructor(config: Partial<RateLimitConfig> = {}) {
    this.config = { ...DEFAULT_CONFIG, ...config }
  }

  /**
   * Calculate delay with exponential backoff and jitter
   */
  calculateDelay(attempt: number, retryAfter?: number): number {
    // If server provides retry-after, use it
    if (retryAfter && retryAfter > 0) {
      return Math.min(retryAfter * 1000, this.config.maxDelayMs)
    }

    // Exponential backoff: baseDelay * 2^attempt
    const exponentialDelay = this.config.baseDelayMs * Math.pow(2, attempt)

    // Add jitter: +/- jitterFactor%
    const jitter = exponentialDelay * this.config.jitterFactor * (Math.random() * 2 - 1)

    // Clamp to max delay
    return Math.min(exponentialDelay + jitter, this.config.maxDelayMs)
  }

  /**
   * Check if a provider is currently in cooldown
   */
  isInCooldown(provider: string): boolean {
    const cooldownEnd = this.cooldowns.get(provider)
    if (!cooldownEnd) return false
    return Date.now() < cooldownEnd
  }

  /**
   * Set cooldown for a provider
   */
  setCooldown(provider: string, durationMs: number): void {
    this.cooldowns.set(provider, Date.now() + durationMs)
  }

  /**
   * Get remaining cooldown time for a provider
   */
  getCooldownRemaining(provider: string): number {
    const cooldownEnd = this.cooldowns.get(provider)
    if (!cooldownEnd) return 0
    return Math.max(0, cooldownEnd - Date.now())
  }

  /**
   * Clear cooldown for a provider
   */
  clearCooldown(provider: string): void {
    this.cooldowns.delete(provider)
  }

  /**
   * Check if error is a rate limit error
   */
  isRateLimitError(error: unknown): error is RateLimitError {
    if (typeof error !== 'object' || error === null) return false

    const err = error as Record<string, unknown>

    // Check for 429 status
    if (err.status === 429) return true

    // Check for rate limit in message
    const message = (err.message as string) || ''
    if (message.toLowerCase().includes('rate limit')) return true
    if (message.toLowerCase().includes('too many requests')) return true

    return false
  }

  /**
   * Extract provider from model string
   */
  getProvider(model: string): string {
    const parts = model.split('/')
    return parts[0] || 'unknown'
  }

  /**
   * Get fallback models for a given model
   */
  getFallbackModels(model: string): string[] {
    return FALLBACK_MODELS[model] || []
  }

  /**
   * Sleep for specified duration
   */
  private async sleep(ms: number): Promise<void> {
    return new Promise(resolve => setTimeout(resolve, ms))
  }

  /**
   * Execute a function with retry logic
   */
  async withRetry<T>(
    fn: (model: string) => Promise<T>,
    model: string,
    options: {
      /** Allow fallback to alternative models */
      allowFallback?: boolean
      /** Custom retry handler */
      onRetry?: (attempt: number, delay: number, error: unknown) => void
    } = {}
  ): Promise<RetryResult<T>> {
    const { allowFallback = true, onRetry } = options

    let attempts = 0
    let totalDelay = 0
    let usedFallback = false
    let currentModel = model
    let fallbackModel: string | undefined

    // Try with primary model first
    while (attempts < this.config.maxRetries) {
      attempts++

      // Check cooldown
      const provider = this.getProvider(currentModel)
      const cooldownRemaining = this.getCooldownRemaining(provider)

      if (cooldownRemaining > 0) {
        // If in cooldown, try fallback immediately
        if (allowFallback && !usedFallback) {
          const fallbacks = this.getFallbackModels(model)
          for (const fb of fallbacks) {
            const fbProvider = this.getProvider(fb)
            if (!this.isInCooldown(fbProvider)) {
              currentModel = fb
              fallbackModel = fb
              usedFallback = true
              break
            }
          }
        }

        // If still in cooldown, wait
        if (this.getCooldownRemaining(this.getProvider(currentModel)) > 0) {
          await this.sleep(cooldownRemaining)
          totalDelay += cooldownRemaining
        }
      }

      try {
        const result = await fn(currentModel)
        return {
          success: true,
          result,
          attempts,
          totalDelay,
          usedFallback,
          fallbackModel,
        }
      } catch (error) {
        // Check if rate limit error
        if (this.isRateLimitError(error)) {
          const rateLimitError = error as RateLimitError
          const delay = this.calculateDelay(attempts, rateLimitError.retryAfter)

          // Set cooldown for provider
          this.setCooldown(this.getProvider(currentModel), delay)

          // Notify retry handler
          if (onRetry) {
            onRetry(attempts, delay, error)
          }

          // Try fallback if available
          if (allowFallback && !usedFallback) {
            const fallbacks = this.getFallbackModels(model)
            for (const fb of fallbacks) {
              if (!this.isInCooldown(this.getProvider(fb))) {
                currentModel = fb
                fallbackModel = fb
                usedFallback = true
                break
              }
            }
          }

          // Wait before retry
          await this.sleep(delay)
          totalDelay += delay
        } else {
          // Not a rate limit error, don't retry
          return {
            success: false,
            error: error instanceof Error ? error : new Error(String(error)),
            attempts,
            totalDelay,
            usedFallback,
            fallbackModel,
          }
        }
      }
    }

    // Max retries exceeded
    return {
      success: false,
      error: new Error(`Max retries (${this.config.maxRetries}) exceeded for model ${model}`),
      attempts,
      totalDelay,
      usedFallback,
      fallbackModel,
    }
  }
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Create a rate limiter with default config
 */
export function createRateLimiter(config?: Partial<RateLimitConfig>): RateLimiter {
  return new RateLimiter(config)
}

/**
 * Get best available fallback model
 */
export function getBestFallback(model: string, unavailable: Set<string> = new Set()): string | null {
  const fallbacks = FALLBACK_MODELS[model] || []

  for (const fallback of fallbacks) {
    if (!unavailable.has(fallback)) {
      return fallback
    }
  }

  return null
}

/**
 * Describe retry result in human-readable format
 */
export function describeRetryResult<T>(result: RetryResult<T>): string {
  const lines: string[] = []

  if (result.success) {
    lines.push('✅ Operation succeeded')
  } else {
    lines.push('❌ Operation failed')
    if (result.error) {
      lines.push(`Error: ${result.error.message}`)
    }
  }

  lines.push(`Attempts: ${result.attempts}`)

  if (result.totalDelay > 0) {
    lines.push(`Total delay: ${(result.totalDelay / 1000).toFixed(1)}s`)
  }

  if (result.usedFallback && result.fallbackModel) {
    lines.push(`Used fallback: ${result.fallbackModel}`)
  }

  return lines.join('\n')
}
