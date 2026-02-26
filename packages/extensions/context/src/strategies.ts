/**
 * Context compaction strategies.
 * Reduce message history to fit within token budgets.
 */

import type { ContextStrategy } from '@ava/core-v2/extensions'
import type { ChatMessage } from '@ava/core-v2/llm'

/**
 * Simple truncation — keep the newest messages that fit.
 */
export const truncateStrategy: ContextStrategy = {
  name: 'truncate',
  description: 'Keep the most recent messages, dropping oldest first.',
  compact(messages: ChatMessage[], targetTokens: number): ChatMessage[] {
    // Rough estimate: 4 chars per token
    const charLimit = targetTokens * 4
    let totalChars = 0
    const result: ChatMessage[] = []

    // Always keep the system message
    const system = messages.find((m) => m.role === 'system')
    if (system) {
      result.push(system)
      totalChars += system.content.length
    }

    // Add messages from newest to oldest
    const nonSystem = messages.filter((m) => m.role !== 'system').reverse()
    for (const msg of nonSystem) {
      if (totalChars + msg.content.length > charLimit) break
      result.unshift(msg)
      totalChars += msg.content.length
    }

    // Put system back at front
    if (system && result[0] !== system) {
      result.unshift(system)
    }

    return result
  },
}

/**
 * Summarize old messages — keep recent, summarize rest.
 * (Simplified version — a full implementation would use LLM for summarization)
 */
export const summarizeStrategy: ContextStrategy = {
  name: 'summarize',
  description: 'Summarize older messages and keep recent ones.',
  compact(messages: ChatMessage[], targetTokens: number): ChatMessage[] {
    const charLimit = targetTokens * 4
    const keepCount = Math.max(4, Math.floor(messages.length / 3))
    const recent = messages.slice(-keepCount)
    const old = messages.slice(0, -keepCount)

    if (old.length === 0) return recent

    // Simple summary: just count what was dropped
    const summary: ChatMessage = {
      role: 'system',
      content: `[Context compacted: ${old.length} earlier messages summarized. The conversation started with the user's request and included ${old.filter((m) => m.role === 'assistant').length} assistant responses.]`,
    }

    const result = [summary, ...recent]

    // If still too long, fall back to truncation
    const totalChars = result.reduce((sum, m) => sum + m.content.length, 0)
    if (totalChars > charLimit) {
      return truncateStrategy.compact(result, targetTokens)
    }

    return result
  },
}

export const ALL_STRATEGIES = [truncateStrategy, summarizeStrategy]
