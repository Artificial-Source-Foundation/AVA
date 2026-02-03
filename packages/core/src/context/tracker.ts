/**
 * Context Tracker
 * Token counting and context window management using gpt-tokenizer
 *
 * Tracks token usage per message and total context consumption.
 * Provides thresholds for triggering compaction strategies.
 */

import { encode } from 'gpt-tokenizer'

import type { ChatMessage } from '../types/llm.js'

// ============================================================================
// Types
// ============================================================================

/** Token statistics for context window */
export interface TokenStats {
  /** Token count per message (messageId -> tokens) */
  messages: Map<string, number>
  /** Total tokens used */
  total: number
  /** Context limit for the model */
  limit: number
  /** Remaining tokens available */
  remaining: number
  /** Percentage of context used (0-100) */
  percentUsed: number
}

/** Content that can be tokenized */
export type TokenizableContent = string | ChatMessage | ChatMessage[]

// ============================================================================
// Token Counting Utilities
// ============================================================================

/**
 * Count tokens for a string using GPT tokenizer
 * Note: This is an approximation - actual token counts vary by model
 */
export function countTokens(content: string): number {
  if (!content) return 0
  return encode(content).length
}

/**
 * Get text content from a ChatMessage for tokenization
 */
export function getMessageText(message: ChatMessage): string {
  // ChatMessage.content is always a string in our current type
  return message.content
}

/**
 * Count tokens for a single message including role overhead
 * Accounts for message structure tokens (role, content markers)
 */
export function countMessageTokens(message: ChatMessage): number {
  // Base overhead for message structure (~4 tokens per message)
  const structureOverhead = 4

  const contentTokens = countTokens(getMessageText(message))

  return structureOverhead + contentTokens
}

/**
 * Count tokens for an array of messages
 */
export function countMessagesTokens(messages: ChatMessage[]): number {
  // Base overhead for message array (~3 tokens)
  const arrayOverhead = 3

  const messageTokens = messages.reduce((sum, msg) => sum + countMessageTokens(msg), 0)

  return arrayOverhead + messageTokens
}

// ============================================================================
// Context Tracker Class
// ============================================================================

/**
 * Tracks token usage across conversation history
 *
 * Usage:
 * ```ts
 * const tracker = new ContextTracker(200000) // Claude's 200k limit
 *
 * // Track messages
 * tracker.addMessage('msg-1', message)
 * tracker.addMessage('msg-2', 'Hello, world!')
 *
 * // Check if compaction needed
 * if (tracker.shouldCompact()) {
 *   // Trigger compaction strategy
 * }
 *
 * // Get current stats
 * const stats = tracker.getStats()
 * console.log(`Using ${stats.percentUsed.toFixed(1)}% of context`)
 * ```
 */
export class ContextTracker {
  private stats: TokenStats

  constructor(contextLimit: number) {
    this.stats = {
      messages: new Map(),
      total: 0,
      limit: contextLimit,
      remaining: contextLimit,
      percentUsed: 0,
    }
  }

  // ==========================================================================
  // Message Tracking
  // ==========================================================================

  /**
   * Add a message and track its token count
   * @param id - Unique message identifier
   * @param content - Message content (string or ChatMessage)
   * @returns Token count for the added message
   */
  addMessage(id: string, content: string | ChatMessage): number {
    let tokens: number

    if (typeof content === 'string') {
      tokens = countTokens(content)
    } else {
      tokens = countMessageTokens(content)
    }

    // If message already exists, remove old count first
    if (this.stats.messages.has(id)) {
      this.removeMessage(id)
    }

    this.stats.messages.set(id, tokens)
    this.updateTotals()

    return tokens
  }

  /**
   * Add multiple messages at once
   * @param messages - Array of [id, content] pairs or [id, ChatMessage] pairs
   * @returns Total tokens added
   */
  addMessages(messages: Array<[string, string | ChatMessage]>): number {
    let totalAdded = 0
    for (const [id, content] of messages) {
      totalAdded += this.addMessage(id, content)
    }
    return totalAdded
  }

  /**
   * Remove a message from tracking
   * @param id - Message identifier to remove
   */
  removeMessage(id: string): void {
    this.stats.messages.delete(id)
    this.updateTotals()
  }

  /**
   * Remove multiple messages at once
   * @param ids - Message identifiers to remove
   */
  removeMessages(ids: string[]): void {
    for (const id of ids) {
      this.stats.messages.delete(id)
    }
    this.updateTotals()
  }

  /**
   * Update token count for an existing message
   * @param id - Message identifier
   * @param content - New content
   * @returns New token count, or undefined if message not found
   */
  updateMessage(id: string, content: string | ChatMessage): number | undefined {
    if (!this.stats.messages.has(id)) {
      return undefined
    }
    return this.addMessage(id, content)
  }

  /**
   * Clear all tracked messages
   */
  clear(): void {
    this.stats.messages.clear()
    this.updateTotals()
  }

  // ==========================================================================
  // Stats and Thresholds
  // ==========================================================================

  /**
   * Get token count for a specific message
   * @param id - Message identifier
   * @returns Token count or undefined if not tracked
   */
  getMessageTokens(id: string): number | undefined {
    return this.stats.messages.get(id)
  }

  /**
   * Get current token statistics
   * Returns a copy to prevent external mutation
   */
  getStats(): TokenStats {
    return {
      ...this.stats,
      messages: new Map(this.stats.messages),
    }
  }

  /**
   * Check if context usage exceeds threshold
   * @param threshold - Percentage threshold (default: 80)
   * @returns true if usage >= threshold
   */
  shouldCompact(threshold = 80): boolean {
    return this.stats.percentUsed >= threshold
  }

  /**
   * Check if adding content would exceed limit
   * @param content - Content to check
   * @param buffer - Safety buffer in tokens (default: 1000)
   * @returns true if content would fit within limit - buffer
   */
  wouldFit(content: string | ChatMessage, buffer = 1000): boolean {
    const tokens = typeof content === 'string' ? countTokens(content) : countMessageTokens(content)

    return this.stats.remaining - buffer >= tokens
  }

  /**
   * Get available tokens with safety buffer
   * @param buffer - Safety buffer in tokens (default: 1000)
   * @returns Tokens available for new content
   */
  getAvailable(buffer = 1000): number {
    return Math.max(0, this.stats.remaining - buffer)
  }

  /**
   * Update context limit (e.g., when switching models)
   * @param newLimit - New context window size
   */
  setLimit(newLimit: number): void {
    this.stats.limit = newLimit
    this.updateTotals()
  }

  // ==========================================================================
  // Internal Helpers
  // ==========================================================================

  private updateTotals(): void {
    let total = 0
    for (const tokens of this.stats.messages.values()) {
      total += tokens
    }

    this.stats.total = total
    this.stats.remaining = this.stats.limit - total
    this.stats.percentUsed = this.stats.limit > 0 ? (total / this.stats.limit) * 100 : 0
  }
}

// ============================================================================
// Factory Function
// ============================================================================

/**
 * Create a new context tracker
 * @param contextLimit - Maximum tokens for the context window
 */
export function createContextTracker(contextLimit: number): ContextTracker {
  return new ContextTracker(contextLimit)
}
