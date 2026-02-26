/**
 * Visibility-Aware Compaction Strategy
 *
 * Instead of removing older messages entirely, tags them as 'agent_visible'.
 * This preserves context for the agent while keeping the UI clean.
 *
 * Inspired by Goose's user_visible + agent_visible flags.
 *
 * Pros:
 * - Agents retain older context (not lost entirely)
 * - UI stays clean (compacted messages hidden)
 * - Gradual degradation instead of hard cut
 *
 * Cons:
 * - Doesn't reduce token count (messages still sent to LLM)
 * - Must be combined with another strategy for actual compaction
 */

import type { CompactionStrategy, Message, MessageVisibility } from '../types.js'

// ============================================================================
// Helpers
// ============================================================================

/**
 * Check if a message is visible to the user
 */
export function isUserVisible(message: Message): boolean {
  const v = message.visibility ?? 'all'
  return v === 'all' || v === 'user_visible'
}

/**
 * Check if a message should be sent to the LLM
 */
export function isAgentVisible(message: Message): boolean {
  const v = message.visibility ?? 'all'
  return v === 'all' || v === 'agent_visible'
}

/**
 * Filter messages to only those visible to the user
 */
export function filterUserVisible(messages: Message[]): Message[] {
  return messages.filter(isUserVisible)
}

/**
 * Filter messages to only those visible to the agent/LLM
 */
export function filterAgentVisible(messages: Message[]): Message[] {
  return messages.filter(isAgentVisible)
}

/**
 * Tag a message with a visibility level
 */
export function tagVisibility(message: Message, visibility: MessageVisibility): Message {
  return { ...message, visibility }
}

// ============================================================================
// Strategy
// ============================================================================

export interface VisibilityCompactionOptions {
  /** Number of recent messages to keep fully visible (default: 6) */
  preserveRecent?: number
}

/**
 * Visibility-aware compaction strategy
 *
 * Tags older messages as 'agent_visible' (hidden from UI, still sent to LLM).
 * The most recent messages remain 'all' (visible everywhere).
 *
 * Note: This strategy does NOT reduce token count — it only changes visibility.
 * Combine with another strategy (e.g. sliding-window) for actual token reduction.
 */
export function createVisibilityCompaction(
  options: VisibilityCompactionOptions = {}
): CompactionStrategy {
  const { preserveRecent = 6 } = options

  return {
    name: 'visibility',

    async compact(messages: Message[], _targetTokens: number): Promise<Message[]> {
      if (messages.length === 0) {
        return []
      }

      // Separate system messages (always visible)
      const systemMessages = messages.filter((m) => m.role === 'system')
      const conversationMessages = messages.filter((m) => m.role !== 'system')

      if (conversationMessages.length <= preserveRecent) {
        return messages
      }

      // Tag older messages as agent_visible
      const olderMessages = conversationMessages.slice(0, -preserveRecent)
      const recentMessages = conversationMessages.slice(-preserveRecent)

      const taggedOlder = olderMessages.map((m) => tagVisibility(m, 'agent_visible'))

      return [...systemMessages, ...taggedOlder, ...recentMessages]
    },
  }
}

/**
 * Default visibility compaction strategy instance
 */
export const visibilityCompaction = createVisibilityCompaction()
