/**
 * Repetition Inspector
 * Detects stuck tool-call patterns at the per-call level
 *
 * Complements session-level doom-loop detection (session/doom-loop.ts)
 * by operating on individual tool calls within a configurable time window.
 */

// ============================================================================
// Types
// ============================================================================

/** Configuration for repetition detection */
export interface RepetitionConfig {
  /** Number of identical calls to trigger detection (default: 3) */
  threshold: number
  /** Time window in ms to consider (default: 60000 = 1 minute) */
  windowMs: number
  /** Maximum history entries to keep (default: 50) */
  maxHistory: number
}

/** A recorded call for repetition tracking */
interface CallRecord {
  tool: string
  hash: string
  timestamp: number
}

/** Result of a repetition check */
export interface RepetitionResult {
  /** Whether repetition was detected */
  detected: boolean
  /** Number of identical calls in the window */
  count: number
  /** Reason (if detected) */
  reason: string
}

// ============================================================================
// Defaults
// ============================================================================

const DEFAULT_CONFIG: RepetitionConfig = {
  threshold: 3,
  windowMs: 60_000,
  maxHistory: 50,
}

// ============================================================================
// RepetitionInspector
// ============================================================================

/**
 * Detects when the same tool call is made repeatedly in a short window
 */
export class RepetitionInspector {
  private config: RepetitionConfig
  private history: CallRecord[] = []

  constructor(config: Partial<RepetitionConfig> = {}) {
    this.config = { ...DEFAULT_CONFIG, ...config }
  }

  /**
   * Check a tool call for repetition
   */
  check(tool: string, params: Record<string, unknown>): RepetitionResult {
    const now = Date.now()
    const hash = this.hashParams(tool, params)

    // Add to history
    this.history.push({ tool, hash, timestamp: now })

    // Trim old entries outside the window
    const cutoff = now - this.config.windowMs
    this.history = this.history.filter((r) => r.timestamp >= cutoff)

    // Also trim by max size
    if (this.history.length > this.config.maxHistory) {
      this.history = this.history.slice(-this.config.maxHistory)
    }

    // Count identical calls in the window
    const count = this.history.filter((r) => r.hash === hash).length

    if (count >= this.config.threshold) {
      return {
        detected: true,
        count,
        reason: `Tool "${tool}" called ${count} times with identical params in ${this.config.windowMs / 1000}s window`,
      }
    }

    return {
      detected: false,
      count,
      reason: '',
    }
  }

  /**
   * Hash tool + params for comparison
   */
  private hashParams(tool: string, params: Record<string, unknown>): string {
    try {
      const sorted = Object.keys(params)
        .sort()
        .reduce(
          (acc, key) => {
            acc[key] = params[key]
            return acc
          },
          {} as Record<string, unknown>
        )
      return `${tool}:${JSON.stringify(sorted)}`
    } catch {
      // Fallback for circular or non-serializable params
      return `${tool}:[unserializable]`
    }
  }

  /**
   * Clear history
   */
  clear(): void {
    this.history = []
  }

  /**
   * Get current history length
   */
  get historyLength(): number {
    return this.history.length
  }

  /**
   * Update configuration
   */
  configure(config: Partial<RepetitionConfig>): void {
    this.config = { ...this.config, ...config }
  }

  /**
   * Get current configuration
   */
  getConfig(): RepetitionConfig {
    return { ...this.config }
  }
}
