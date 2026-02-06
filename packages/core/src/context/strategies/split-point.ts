/**
 * Safe Split Point Detection
 *
 * Finds safe boundaries in conversation history for splitting
 * older messages (to summarize) from recent ones (to preserve).
 *
 * Key insight from Gemini CLI: Don't split in the middle of a
 * tool call sequence (user → assistant → tool result). Only
 * split on user message boundaries.
 *
 * Usage:
 * ```ts
 * const splitIdx = findSafeSplitPoint(messages, 0.3)
 * const older = messages.slice(0, splitIdx)
 * const recent = messages.slice(splitIdx)
 * ```
 */

import type { Message } from '../types.js'

// ============================================================================
// Constants
// ============================================================================

/** Default: preserve the last 30% of conversation */
export const DEFAULT_PRESERVE_FRACTION = 0.3

/** Minimum messages to preserve (never split below this) */
export const MIN_PRESERVE_MESSAGES = 4

// ============================================================================
// Split Point Detection
// ============================================================================

/**
 * Find a safe split point in the conversation history.
 *
 * A "safe" split point is at a user message boundary, not in the
 * middle of an assistant response or tool call sequence.
 *
 * @param messages - Full conversation (excluding system message)
 * @param preserveFraction - Fraction of messages to preserve at end (0.0 - 1.0)
 * @returns Index where to split (everything before = summarize, after = keep)
 */
export function findSafeSplitPoint(
  messages: Message[],
  preserveFraction = DEFAULT_PRESERVE_FRACTION
): number {
  if (messages.length <= MIN_PRESERVE_MESSAGES) {
    return 0 // Don't split small conversations
  }

  // Calculate target split position (from the start)
  const maxSplitIndex = messages.length - MIN_PRESERVE_MESSAGES
  const targetIndex = Math.min(Math.floor(messages.length * (1 - preserveFraction)), maxSplitIndex)

  if (targetIndex <= 0) return 0
  if (targetIndex >= messages.length) return messages.length

  // Search backward from target for a safe split point
  // Safe = just before a user message (not in middle of assistant/tool sequence)
  for (let i = targetIndex; i > 0; i--) {
    if (isSafeSplitBefore(messages, i)) {
      return i
    }
  }

  // Search forward from target as fallback
  for (let i = targetIndex + 1; i < messages.length - MIN_PRESERVE_MESSAGES; i++) {
    if (isSafeSplitBefore(messages, i)) {
      return i
    }
  }

  // Absolute fallback: split at target (imperfect but usable)
  return targetIndex
}

/**
 * Check if it's safe to split just before messages[index].
 *
 * Safe conditions:
 * - The message at index is from the user (start of a new turn)
 * - The message before is NOT a partial assistant turn without its tool result
 */
function isSafeSplitBefore(messages: Message[], index: number): boolean {
  if (index <= 0 || index >= messages.length) return false

  const currentMsg = messages[index]!

  // Best case: splitting before a user message = clean turn boundary
  if (currentMsg.role === 'user') {
    return true
  }

  return false
}

/**
 * Find all valid split points in a conversation.
 * Returns indices where it's safe to split (before user messages).
 *
 * Useful for UI or debug purposes.
 */
export function findAllSplitPoints(messages: Message[]): number[] {
  const points: number[] = []

  for (let i = 1; i < messages.length - MIN_PRESERVE_MESSAGES; i++) {
    if (isSafeSplitBefore(messages, i)) {
      points.push(i)
    }
  }

  return points
}

/**
 * Calculate the character count up to a given index.
 * Useful for weighted splitting based on content size rather than message count.
 */
export function getContentSizeUpTo(messages: Message[], endIndex: number): number {
  let size = 0
  for (let i = 0; i < endIndex && i < messages.length; i++) {
    size += messages[i]!.content.length
  }
  return size
}

/**
 * Find split point based on content size rather than message count.
 * This is more accurate when messages have very different sizes.
 *
 * @param messages - Conversation messages
 * @param preserveFraction - Fraction of total content to preserve (by size)
 * @returns Safe split index
 */
export function findSizeSplitPoint(
  messages: Message[],
  preserveFraction = DEFAULT_PRESERVE_FRACTION
): number {
  if (messages.length <= MIN_PRESERVE_MESSAGES) return 0

  const totalSize = getContentSizeUpTo(messages, messages.length)
  const targetCutoffSize = totalSize * (1 - preserveFraction)

  let accumulatedSize = 0
  let targetIndex = 0

  for (let i = 0; i < messages.length; i++) {
    accumulatedSize += messages[i]!.content.length
    if (accumulatedSize >= targetCutoffSize) {
      targetIndex = i
      break
    }
  }

  // Snap to nearest safe split point
  // Search backward first
  for (let i = targetIndex; i > 0; i--) {
    if (isSafeSplitBefore(messages, i)) {
      return i
    }
  }

  // Search forward
  for (let i = targetIndex + 1; i < messages.length - MIN_PRESERVE_MESSAGES; i++) {
    if (isSafeSplitBefore(messages, i)) {
      return i
    }
  }

  return targetIndex
}
