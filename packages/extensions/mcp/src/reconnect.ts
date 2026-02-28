/**
 * Reconnection strategy — exponential backoff with jitter.
 */

export interface ReconnectConfig {
  maxAttempts: number
  baseDelayMs: number
  maxDelayMs: number
}

export const DEFAULT_RECONNECT_CONFIG: ReconnectConfig = {
  maxAttempts: 5,
  baseDelayMs: 1000,
  maxDelayMs: 30_000,
}

export class ReconnectStrategy {
  private attempts = 0
  private config: ReconnectConfig

  constructor(config?: Partial<ReconnectConfig>) {
    this.config = { ...DEFAULT_RECONNECT_CONFIG, ...config }
  }

  /** Calculate delay for the next reconnect attempt. Returns null if max attempts exceeded. */
  nextDelay(): number | null {
    if (this.attempts >= this.config.maxAttempts) return null
    this.attempts++

    // Exponential backoff: base * 2^(attempt-1)
    const exponentialDelay = this.config.baseDelayMs * 2 ** (this.attempts - 1)
    const capped = Math.min(exponentialDelay, this.config.maxDelayMs)

    // Add jitter: 0.5x to 1.5x
    const jitter = 0.5 + Math.random()
    return Math.floor(capped * jitter)
  }

  /** Reset attempt counter (e.g. after successful connection). */
  reset(): void {
    this.attempts = 0
  }

  get attemptCount(): number {
    return this.attempts
  }

  get canRetry(): boolean {
    return this.attempts < this.config.maxAttempts
  }
}
