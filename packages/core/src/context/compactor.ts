/**
 * Context Compactor
 *
 * Orchestrates compaction strategies to reduce context window usage.
 * Tries strategies in order until one succeeds, with fallback handling.
 *
 * Usage:
 * ```ts
 * const compactor = new Compactor({
 *   strategies: [slidingWindow, summarize],
 *   tracker: contextTracker,
 *   targetPercent: 50,
 * })
 *
 * const compacted = await compactor.compact(messages)
 * ```
 */

import { createSlidingWindow, slidingWindow } from './strategies/sliding-window.js'
import type { ContextTracker } from './tracker.js'
import { countMessagesTokens } from './tracker.js'
import type { CompactionOptions, CompactionResult, CompactionStrategy, Message } from './types.js'

// ============================================================================
// Types
// ============================================================================

export interface CompactorConfig {
  /** Strategies to try in order */
  strategies?: CompactionStrategy[]
  /** Context tracker for token counting */
  tracker: ContextTracker
  /** Default target percentage (default: 50) */
  targetPercent?: number
  /** Minimum messages to keep on fallback (default: 10) */
  fallbackMinMessages?: number
}

// ============================================================================
// Compactor Class
// ============================================================================

/**
 * Context compactor that manages multiple strategies
 */
export class Compactor {
  private readonly strategies: CompactionStrategy[]
  private readonly tracker: ContextTracker
  private readonly defaultTargetPercent: number
  private readonly fallbackMinMessages: number

  constructor(config: CompactorConfig) {
    this.strategies = config.strategies ?? [slidingWindow]
    this.tracker = config.tracker
    this.defaultTargetPercent = config.targetPercent ?? 50
    this.fallbackMinMessages = config.fallbackMinMessages ?? 10
  }

  // ==========================================================================
  // Main Compaction
  // ==========================================================================

  /**
   * Compact messages using configured strategies
   *
   * @param messages - Messages to compact
   * @param options - Compaction options
   * @returns Compacted messages with metadata
   */
  async compact(messages: Message[], options: CompactionOptions = {}): Promise<CompactionResult> {
    const {
      targetPercent = this.defaultTargetPercent,
      preserveSystem = true,
      minMessages = 4,
    } = options

    const stats = this.tracker.getStats()
    const targetTokens = Math.floor(stats.limit * (targetPercent / 100))
    const originalCount = messages.length
    const originalTokens = countMessagesTokens(messages)

    // Check if compaction is needed
    if (originalTokens <= targetTokens) {
      return {
        messages,
        originalCount,
        compactedCount: messages.length,
        tokensSaved: 0,
        strategyUsed: 'none',
      }
    }

    // Ensure minimum messages
    if (messages.length <= minMessages) {
      return {
        messages,
        originalCount,
        compactedCount: messages.length,
        tokensSaved: 0,
        strategyUsed: 'none',
      }
    }

    // Try each strategy in order
    for (const strategy of this.strategies) {
      try {
        const compacted = await strategy.compact(messages, targetTokens)

        // Validate result
        if (compacted.length > 0) {
          const compactedTokens = countMessagesTokens(compacted)

          // Check if compaction was effective
          if (compactedTokens < originalTokens) {
            return {
              messages: compacted,
              originalCount,
              compactedCount: compacted.length,
              tokensSaved: originalTokens - compactedTokens,
              strategyUsed: strategy.name,
            }
          }
        }
      } catch (err) {
        console.warn(`Compaction strategy '${strategy.name}' failed:`, err)
        // Continue to next strategy
      }
    }

    // Fallback: keep last N messages
    const fallbackMessages = this.fallbackCompact(messages, preserveSystem)

    return {
      messages: fallbackMessages,
      originalCount,
      compactedCount: fallbackMessages.length,
      tokensSaved: originalTokens - countMessagesTokens(fallbackMessages),
      strategyUsed: 'fallback',
    }
  }

  /**
   * Quick compaction check without running strategies
   * Returns true if messages exceed the target threshold
   */
  needsCompaction(targetPercent?: number): boolean {
    const threshold = targetPercent ?? this.defaultTargetPercent
    return this.tracker.shouldCompact(threshold)
  }

  /**
   * Get current context usage percentage
   */
  getUsagePercent(): number {
    return this.tracker.getStats().percentUsed
  }

  // ==========================================================================
  // Strategy Management
  // ==========================================================================

  /**
   * Add a strategy to the end of the strategy list
   */
  addStrategy(strategy: CompactionStrategy): void {
    this.strategies.push(strategy)
  }

  /**
   * Insert a strategy at a specific position
   */
  insertStrategy(strategy: CompactionStrategy, index: number): void {
    this.strategies.splice(index, 0, strategy)
  }

  /**
   * Remove a strategy by name
   */
  removeStrategy(name: string): boolean {
    const index = this.strategies.findIndex((s) => s.name === name)
    if (index !== -1) {
      this.strategies.splice(index, 1)
      return true
    }
    return false
  }

  /**
   * Get list of configured strategy names
   */
  getStrategyNames(): string[] {
    return this.strategies.map((s) => s.name)
  }

  // ==========================================================================
  // Helpers
  // ==========================================================================

  private fallbackCompact(messages: Message[], preserveSystem: boolean): Message[] {
    // Separate system message
    const systemMessages = preserveSystem ? messages.filter((m) => m.role === 'system') : []
    const conversationMessages = messages.filter((m) => m.role !== 'system')

    // Keep last N messages
    const kept = conversationMessages.slice(-this.fallbackMinMessages)

    return [...systemMessages, ...kept]
  }
}

// ============================================================================
// Factory Functions
// ============================================================================

/**
 * Create a compactor with default sliding window strategy
 */
export function createCompactor(tracker: ContextTracker, targetPercent = 50): Compactor {
  return new Compactor({
    strategies: [slidingWindow],
    tracker,
    targetPercent,
  })
}

/**
 * Create a compactor with aggressive sliding window (for when LLM summaries aren't available)
 */
export function createAggressiveCompactor(tracker: ContextTracker, targetPercent = 30): Compactor {
  return new Compactor({
    strategies: [createSlidingWindow({ minMessages: 4, ensureValidTurns: true })],
    tracker,
    targetPercent,
    fallbackMinMessages: 4,
  })
}

// ============================================================================
// Auto-Compaction Hook
// ============================================================================

/**
 * Create an auto-compaction function that runs when threshold is exceeded
 *
 * @example
 * ```ts
 * const autoCompact = createAutoCompactor(compactor, {
 *   threshold: 80,
 *   targetPercent: 50,
 * })
 *
 * // Call after adding messages
 * const messages = await autoCompact(currentMessages)
 * ```
 */
export function createAutoCompactor(
  compactor: Compactor,
  options: { threshold?: number; targetPercent?: number } = {}
): (messages: Message[]) => Promise<Message[]> {
  const { threshold = 80, targetPercent = 50 } = options

  return async (messages: Message[]): Promise<Message[]> => {
    if (!compactor.needsCompaction(threshold)) {
      return messages
    }

    const result = await compactor.compact(messages, { targetPercent })
    return result.messages
  }
}
