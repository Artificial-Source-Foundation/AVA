/**
 * Context compaction strategies.
 * Reduce message history to fit within token budgets.
 */

import type { ContextStrategy } from '@ava/core-v2/extensions'
import type { ChatMessage, MessageContent } from '@ava/core-v2/llm'

/** Compute content length for both string and ContentBlock[] messages. */
function contentLength(content: MessageContent): number {
  if (typeof content === 'string') return content.length
  return content.reduce((sum, b) => {
    if (b.type === 'text') return sum + b.text.length
    if (b.type === 'tool_result') return sum + b.content.length
    // tool_use: count name + serialized input
    return sum + b.name.length + JSON.stringify(b.input).length
  }, 0)
}

/** Check if a message contains tool_use blocks (assistant response with tool calls). */
function hasToolUse(msg: ChatMessage): boolean {
  if (typeof msg.content === 'string') return false
  return msg.content.some((b) => b.type === 'tool_use')
}

/** Check if a message contains tool_result blocks (user message with tool results). */
function hasToolResult(msg: ChatMessage): boolean {
  if (typeof msg.content === 'string') return false
  return msg.content.some((b) => b.type === 'tool_result')
}

/**
 * Simple truncation — keep the newest messages that fit.
 * Preserves paired tool_use/tool_result messages together.
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
      totalChars += contentLength(system.content)
    }

    // Add messages from newest to oldest, keeping pairs together
    const nonSystem = messages.filter((m) => m.role !== 'system')

    // Group paired messages (assistant with tool_use + user with tool_result)
    const groups: ChatMessage[][] = []
    let i = nonSystem.length - 1
    while (i >= 0) {
      const msg = nonSystem[i]!
      // If this is a tool_result user message, pair it with the preceding assistant
      if (hasToolResult(msg) && i > 0 && hasToolUse(nonSystem[i - 1]!)) {
        groups.push([nonSystem[i - 1]!, msg])
        i -= 2
      } else {
        groups.push([msg])
        i--
      }
    }

    // groups are newest-first; add them until we hit the limit
    for (const group of groups) {
      const groupLen = group.reduce((sum, m) => sum + contentLength(m.content), 0)
      if (totalChars + groupLen > charLimit) break
      // Prepend the group (they're in order within the group)
      result.splice(system ? 1 : 0, 0, ...group)
      totalChars += groupLen
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
    const totalChars = result.reduce((sum, m) => sum + contentLength(m.content), 0)
    if (totalChars > charLimit) {
      return truncateStrategy.compact(result, targetTokens)
    }

    return result
  },
}

export const ALL_STRATEGIES = [truncateStrategy, summarizeStrategy]
