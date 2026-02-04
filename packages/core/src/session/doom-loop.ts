/**
 * Doom Loop Detection
 * Detect and prevent infinite loops in tool calls
 *
 * A doom loop occurs when the LLM repeatedly makes identical tool calls,
 * typically indicating it's stuck and unable to make progress.
 */

// ============================================================================
// Types
// ============================================================================

/**
 * A recorded tool call for loop detection
 */
export interface RecordedToolCall {
  /** Tool name */
  tool: string
  /** Stringified parameters (for comparison) */
  paramsHash: string
  /** Timestamp */
  timestamp: number
}

/**
 * Configuration for doom loop detection
 */
export interface DoomLoopConfig {
  /** Number of identical consecutive calls to trigger detection (default: 3) */
  threshold: number
  /** Maximum history size to track (default: 10) */
  historySize: number
  /** Whether to auto-block or just warn (default: false = warn only) */
  autoBlock: boolean
}

/**
 * Result of doom loop check
 */
export interface DoomLoopCheckResult {
  /** Whether a doom loop was detected */
  detected: boolean
  /** Number of consecutive identical calls */
  consecutiveCount: number
  /** The repeated tool call, if detected */
  repeatedCall?: { tool: string; params: Record<string, unknown> }
  /** Suggested action */
  suggestion?: string
}

// ============================================================================
// Default Configuration
// ============================================================================

const DEFAULT_CONFIG: DoomLoopConfig = {
  threshold: 3,
  historySize: 10,
  autoBlock: false,
}

// ============================================================================
// Doom Loop Detector
// ============================================================================

/**
 * Doom loop detector instance
 * Tracks tool calls per session to detect repeating patterns
 */
export class DoomLoopDetector {
  private config: DoomLoopConfig
  private history: Map<string, RecordedToolCall[]> = new Map()

  constructor(config: Partial<DoomLoopConfig> = {}) {
    this.config = { ...DEFAULT_CONFIG, ...config }
  }

  /**
   * Hash parameters for comparison
   * Normalizes and stringifies params deterministically
   */
  private hashParams(params: Record<string, unknown>): string {
    try {
      // Sort keys for deterministic comparison
      const sorted = Object.keys(params)
        .sort()
        .reduce(
          (acc, key) => {
            acc[key] = params[key]
            return acc
          },
          {} as Record<string, unknown>
        )
      return JSON.stringify(sorted)
    } catch {
      return JSON.stringify(params)
    }
  }

  /**
   * Record a tool call and check for doom loop
   *
   * @param sessionId - Session identifier
   * @param tool - Tool name
   * @param params - Tool parameters
   * @returns Check result indicating if doom loop detected
   */
  check(sessionId: string, tool: string, params: Record<string, unknown>): DoomLoopCheckResult {
    const paramsHash = this.hashParams(params)
    const now = Date.now()

    // Get or create session history
    let sessionHistory = this.history.get(sessionId)
    if (!sessionHistory) {
      sessionHistory = []
      this.history.set(sessionId, sessionHistory)
    }

    // Add current call to history
    sessionHistory.push({ tool, paramsHash, timestamp: now })

    // Trim history to max size
    if (sessionHistory.length > this.config.historySize) {
      sessionHistory.shift()
    }

    // Check for consecutive identical calls
    let consecutiveCount = 1
    for (let i = sessionHistory.length - 2; i >= 0; i--) {
      const prevCall = sessionHistory[i]
      if (prevCall.tool === tool && prevCall.paramsHash === paramsHash) {
        consecutiveCount++
      } else {
        break
      }
    }

    // Detect doom loop
    if (consecutiveCount >= this.config.threshold) {
      return {
        detected: true,
        consecutiveCount,
        repeatedCall: { tool, params },
        suggestion: `The same ${tool} call has been made ${consecutiveCount} times consecutively. Consider trying a different approach.`,
      }
    }

    return {
      detected: false,
      consecutiveCount,
    }
  }

  /**
   * Clear history for a session
   *
   * @param sessionId - Session to clear, or all if not specified
   */
  clear(sessionId?: string): void {
    if (sessionId) {
      this.history.delete(sessionId)
    } else {
      this.history.clear()
    }
  }

  /**
   * Get history for a session
   *
   * @param sessionId - Session identifier
   * @returns Copy of session history
   */
  getHistory(sessionId: string): RecordedToolCall[] {
    return [...(this.history.get(sessionId) || [])]
  }

  /**
   * Reset a specific tool pattern (allow it again after user intervention)
   *
   * @param sessionId - Session identifier
   * @param tool - Tool name to reset
   */
  resetTool(sessionId: string, tool: string): void {
    const sessionHistory = this.history.get(sessionId)
    if (sessionHistory) {
      // Remove all entries for this tool
      const filtered = sessionHistory.filter((h) => h.tool !== tool)
      this.history.set(sessionId, filtered)
    }
  }

  /**
   * Update configuration
   *
   * @param config - New configuration values
   */
  configure(config: Partial<DoomLoopConfig>): void {
    this.config = { ...this.config, ...config }
  }

  /**
   * Get current configuration
   */
  getConfig(): DoomLoopConfig {
    return { ...this.config }
  }
}

// ============================================================================
// Global Instance
// ============================================================================

/** Global doom loop detector instance */
let globalDetector: DoomLoopDetector | null = null

/**
 * Get the global doom loop detector
 *
 * @param config - Optional configuration overrides
 * @returns Global detector instance
 */
export function getDoomLoopDetector(config?: Partial<DoomLoopConfig>): DoomLoopDetector {
  if (!globalDetector) {
    globalDetector = new DoomLoopDetector(config)
  } else if (config) {
    globalDetector.configure(config)
  }
  return globalDetector
}

/**
 * Check for doom loop using global detector
 *
 * @param sessionId - Session identifier
 * @param tool - Tool name
 * @param params - Tool parameters
 * @returns Check result
 */
export function checkDoomLoop(
  sessionId: string,
  tool: string,
  params: Record<string, unknown>
): DoomLoopCheckResult {
  return getDoomLoopDetector().check(sessionId, tool, params)
}

/**
 * Clear doom loop history
 *
 * @param sessionId - Session to clear, or all if not specified
 */
export function clearDoomLoopHistory(sessionId?: string): void {
  getDoomLoopDetector().clear(sessionId)
}

/**
 * Reset doom loop detector (for testing)
 */
export function resetDoomLoopDetector(): void {
  globalDetector = null
}
