/**
 * Sliding Window Compaction Strategy
 *
 * Keeps the most recent messages that fit within the target token budget.
 * Always preserves system messages and maintains conversation continuity.
 *
 * Pros:
 * - Simple and predictable
 * - No LLM calls required
 * - Fast execution
 *
 * Cons:
 * - Loses older context entirely
 * - No summarization of dropped messages
 */

import { countMessageTokens, countTokens } from '../tracker.js'
import type { CompactionStrategy, Message } from '../types.js'

// ============================================================================
// Strategy Implementation
// ============================================================================

/**
 * Get text content from a Message for tokenization
 */
function getTextContent(message: Message): string {
  return message.content
}

/**
 * Sliding window compaction strategy
 *
 * Algorithm:
 * 1. Separate system message from conversation
 * 2. Start from most recent messages
 * 3. Keep adding messages until target tokens exceeded
 * 4. Prepend system message if present
 *
 * @example
 * ```ts
 * const result = await slidingWindow.compact(messages, 50000)
 * // Returns most recent messages fitting in 50k tokens
 * ```
 */
export const slidingWindow: CompactionStrategy = {
  name: 'sliding-window',

  async compact(messages: Message[], targetTokens: number): Promise<Message[]> {
    if (messages.length === 0) {
      return []
    }

    // Separate system message from conversation
    const systemMessages = messages.filter((m) => m.role === 'system')
    const conversationMessages = messages.filter((m) => m.role !== 'system')

    // Calculate system message overhead
    let systemTokens = 0
    for (const sys of systemMessages) {
      systemTokens += countTokens(getTextContent(sys))
    }

    // Available budget for conversation
    const conversationBudget = targetTokens - systemTokens

    if (conversationBudget <= 0) {
      // Only room for system messages
      return systemMessages
    }

    // Keep messages from end until we hit budget
    const kept: Message[] = []
    let usedTokens = 0

    // Iterate from most recent to oldest
    for (let i = conversationMessages.length - 1; i >= 0; i--) {
      const message = conversationMessages[i]
      const msgTokens = message.tokenCount ?? countMessageTokens(message)

      // Check if adding this message would exceed budget
      if (usedTokens + msgTokens > conversationBudget) {
        break
      }

      // Add message to front of kept array (maintaining order)
      kept.unshift(message)
      usedTokens += msgTokens
    }

    // Return system messages first, then kept conversation
    return [...systemMessages, ...kept]
  },
}

// ============================================================================
// Factory with Options
// ============================================================================

export interface SlidingWindowOptions {
  /** Minimum messages to always keep (default: 2) */
  minMessages?: number
  /** Whether to ensure valid turn structure (user/assistant pairs) */
  ensureValidTurns?: boolean
}

/**
 * Create a sliding window strategy with custom options
 */
export function createSlidingWindow(options: SlidingWindowOptions = {}): CompactionStrategy {
  const { minMessages = 2, ensureValidTurns = true } = options

  return {
    name: 'sliding-window',

    async compact(messages: Message[], targetTokens: number): Promise<Message[]> {
      if (messages.length === 0) {
        return []
      }

      // Separate system messages
      const systemMessages = messages.filter((m) => m.role === 'system')
      const conversationMessages = messages.filter((m) => m.role !== 'system')

      // Calculate system overhead
      let systemTokens = 0
      for (const sys of systemMessages) {
        systemTokens += countTokens(getTextContent(sys))
      }

      const conversationBudget = targetTokens - systemTokens

      if (conversationBudget <= 0) {
        return systemMessages
      }

      // Keep minimum messages regardless of budget
      const minToKeep = Math.min(minMessages, conversationMessages.length)
      const kept: Message[] = []
      let usedTokens = 0

      // First, add minimum messages from end
      for (
        let i = conversationMessages.length - 1;
        i >= conversationMessages.length - minToKeep;
        i--
      ) {
        if (i < 0) break
        const message = conversationMessages[i]
        kept.unshift(message)
        usedTokens += message.tokenCount ?? countMessageTokens(message)
      }

      // Then add more if budget allows
      for (let i = conversationMessages.length - minToKeep - 1; i >= 0; i--) {
        if (i < 0) break
        const message = conversationMessages[i]
        const msgTokens = message.tokenCount ?? countMessageTokens(message)

        if (usedTokens + msgTokens > conversationBudget) {
          break
        }

        kept.unshift(message)
        usedTokens += msgTokens
      }

      // Ensure valid turn structure if requested
      if (ensureValidTurns && kept.length > 0) {
        // First non-system message should be from user
        while (kept.length > 0 && kept[0].role !== 'user') {
          kept.shift()
        }
      }

      return [...systemMessages, ...kept]
    },
  }
}
