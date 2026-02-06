/**
 * Tool Output Truncation Strategy
 *
 * Truncates large tool/function response messages while preserving recent ones.
 * Inspired by Gemini CLI's reverse token budget approach.
 *
 * Algorithm:
 * 1. Iterate messages from newest to oldest
 * 2. Give each tool response a per-response token budget
 * 3. Recent responses get full content, older ones get truncated
 * 4. Truncated responses keep the last N lines (most relevant)
 *
 * This runs BEFORE summarization to reduce token count cheaply
 * (no LLM call required).
 */

import { countTokens } from '../tracker.js'
import type { CompactionStrategy, Message } from '../types.js'

// ============================================================================
// Types
// ============================================================================

export interface ToolTruncationConfig {
  /** Token budget per tool response (default: 50_000) */
  perResponseBudget?: number
  /** Number of trailing lines to keep when truncating (default: 30) */
  truncateKeepLines?: number
  /** Number of recent tool responses to preserve in full (default: 3) */
  preserveRecentCount?: number
  /** Roles considered as tool responses (default: ['assistant']) */
  toolResponseIndicators?: string[]
}

// ============================================================================
// Constants
// ============================================================================

const DEFAULT_PER_RESPONSE_BUDGET = 50_000
const DEFAULT_TRUNCATE_KEEP_LINES = 30
const DEFAULT_PRESERVE_RECENT_COUNT = 3
const TRUNCATION_MARKER = '\n\n[... output truncated, showing last lines ...]\n\n'

// ============================================================================
// Strategy
// ============================================================================

/**
 * Create a tool output truncation strategy.
 *
 * This is a "pre-processing" strategy that reduces the size of large
 * tool outputs without losing the conversation structure. It should be
 * the FIRST strategy in the chain, before summarization.
 */
export function createToolTruncation(config: ToolTruncationConfig = {}): CompactionStrategy {
  const {
    perResponseBudget = DEFAULT_PER_RESPONSE_BUDGET,
    truncateKeepLines = DEFAULT_TRUNCATE_KEEP_LINES,
    preserveRecentCount = DEFAULT_PRESERVE_RECENT_COUNT,
  } = config

  return {
    name: 'tool-truncation',

    async compact(messages: Message[], _targetTokens: number): Promise<Message[]> {
      if (messages.length === 0) return []

      // Count tool responses (assistant messages with tool-like content)
      const toolResponseIndices = findToolResponseIndices(messages)
      if (toolResponseIndices.length === 0) return messages

      // Preserve most recent N tool responses in full
      const protectedIndices = new Set(
        preserveRecentCount > 0 ? toolResponseIndices.slice(-preserveRecentCount) : []
      )

      // Process messages: truncate unprotected tool responses over budget
      const result: Message[] = []
      let anyTruncated = false

      for (let i = 0; i < messages.length; i++) {
        const msg = messages[i]!

        if (toolResponseIndices.includes(i) && !protectedIndices.has(i)) {
          const tokens = msg.tokenCount ?? countTokens(msg.content)

          if (tokens > perResponseBudget) {
            // Truncate: keep last N lines
            const truncated = truncateContent(msg.content, truncateKeepLines)
            result.push({
              ...msg,
              content: truncated,
              tokenCount: countTokens(truncated),
            })
            anyTruncated = true
            continue
          }
        }

        result.push(msg)
      }

      // If nothing was truncated, return original to signal no improvement
      return anyTruncated ? result : messages
    },
  }
}

// ============================================================================
// Helpers
// ============================================================================

/**
 * Find indices of messages that look like tool responses.
 * These are assistant messages with substantial content
 * (long outputs from bash, grep, file reads, etc.)
 */
function findToolResponseIndices(messages: Message[]): number[] {
  const indices: number[] = []

  for (let i = 0; i < messages.length; i++) {
    const msg = messages[i]!
    // Tool responses are typically assistant messages with tool_calls results
    // or long content from tool execution
    if (isToolResponse(msg)) {
      indices.push(i)
    }
  }

  return indices
}

/**
 * Check if a message is a tool response (large assistant output).
 * Heuristic: assistant messages over 200 characters that look like tool output.
 */
function isToolResponse(msg: Message): boolean {
  if (msg.role !== 'assistant') return false
  if (msg.content.length < 200) return false

  // Check for common tool output patterns
  const hasToolPatterns =
    msg.content.includes('```') ||
    msg.content.includes('Error:') ||
    msg.content.includes('\n  ') || // indented code
    msg.content.split('\n').length > 10 // multi-line output

  return hasToolPatterns
}

/**
 * Truncate content to keep the last N lines.
 * Adds a truncation marker at the beginning.
 */
export function truncateContent(content: string, keepLines: number): string {
  const lines = content.split('\n')

  if (lines.length <= keepLines) {
    return content
  }

  const keptLines = lines.slice(-keepLines)
  const droppedCount = lines.length - keepLines
  return `[${droppedCount} lines truncated]${TRUNCATION_MARKER}${keptLines.join('\n')}`
}

// ============================================================================
// Default export
// ============================================================================

export const toolTruncation = createToolTruncation()
