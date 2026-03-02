/**
 * Context compaction strategies.
 * Reduce message history to fit within token budgets.
 */

import type { ContextStrategy } from '@ava/core-v2/extensions'
import type { ChatMessage, ContentBlock, MessageContent } from '@ava/core-v2/llm'

/** Token budget for recent tool results that the prune strategy preserves. */
export const PRUNE_TOKEN_BUDGET = 40_000

/** Tool names whose results are never cleared by the prune strategy. */
export const PROTECTED_TOOLS = new Set(['load_skill', 'skill', 'memory_read'])

/** Rough token estimate: ~4 chars per token. */
export function estimateTokens(text: string): number {
  return Math.ceil(text.length / 4)
}

/** Compute content length for both string and ContentBlock[] messages. */
function contentLength(content: MessageContent): number {
  if (typeof content === 'string') return content.length
  return content.reduce((sum, b) => {
    if (b.type === 'text') return sum + b.text.length
    if (b.type === 'tool_result') return sum + b.content.length
    if (b.type === 'image') return sum + 1000 // rough estimate for images
    // tool_use: count name + serialized input
    return sum + b.name.length + JSON.stringify(b.input).length
  }, 0)
}

/**
 * Build a map of tool_use_id → tool name from all messages.
 * Used to determine which tool produced a given result.
 */
function buildToolUseIdMap(messages: ChatMessage[]): Map<string, string> {
  const map = new Map<string, string>()
  for (const msg of messages) {
    if (typeof msg.content === 'string') continue
    for (const block of msg.content) {
      if (block.type === 'tool_use') {
        map.set(block.id, block.name)
      }
    }
  }
  return map
}

/** Check if a tool_use_id refers to a protected tool (skill, memory_read). */
function isProtectedToolResult(toolUseId: string, toolNameMap: Map<string, string>): boolean {
  const name = toolNameMap.get(toolUseId)
  return name !== undefined && PROTECTED_TOOLS.has(name)
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

/**
 * Prune strategy — clear old tool results that exceed the token budget.
 *
 * Walks backwards through messages, counting tokens in tool_result blocks.
 * Recent results (within PRUNE_TOKEN_BUDGET) are kept intact.
 * Older tool_result blocks are replaced with a placeholder.
 * Protected tools (skill, memory_read, load_skill) are never cleared.
 *
 * This strategy should run BEFORE summarize/truncate in the compaction pipeline
 * since it reduces payload without dropping messages entirely.
 */
export const pruneStrategy: ContextStrategy = {
  name: 'prune',
  description: 'Clear old tool result content, keeping recent results and protected tools.',
  compact(messages: ChatMessage[], _targetTokens: number): ChatMessage[] {
    if (messages.length === 0) return []

    const toolNameMap = buildToolUseIdMap(messages)
    let tokenBudgetRemaining = PRUNE_TOKEN_BUDGET

    // Collect tool_result indices (message index + block index) walking backwards
    // to determine which results are "recent" vs "old"
    interface ToolResultRef {
      msgIndex: number
      blockIndex: number
      tokens: number
      isProtected: boolean
    }
    const refs: ToolResultRef[] = []

    for (let mi = messages.length - 1; mi >= 0; mi--) {
      const msg = messages[mi]!
      if (typeof msg.content === 'string') continue
      for (let bi = msg.content.length - 1; bi >= 0; bi--) {
        const block = msg.content[bi]!
        if (block.type !== 'tool_result') continue
        const tokens = estimateTokens(block.content)
        const prot = isProtectedToolResult(block.tool_use_id, toolNameMap)
        refs.push({ msgIndex: mi, blockIndex: bi, tokens, isProtected: prot })
      }
    }

    // Mark which refs to clear: walk refs (newest-first) and consume budget
    const toClear = new Set<string>() // "msgIndex:blockIndex"
    for (const ref of refs) {
      if (ref.isProtected) continue // never clear protected
      if (tokenBudgetRemaining >= ref.tokens) {
        tokenBudgetRemaining -= ref.tokens
      } else {
        toClear.add(`${ref.msgIndex}:${ref.blockIndex}`)
      }
    }

    if (toClear.size === 0) return messages

    // Deep-clone only messages that need modification
    const result: ChatMessage[] = []
    for (let mi = 0; mi < messages.length; mi++) {
      const msg = messages[mi]!
      if (typeof msg.content === 'string') {
        result.push(msg)
        continue
      }

      // Check if any block in this message needs clearing
      let needsClone = false
      for (let bi = 0; bi < msg.content.length; bi++) {
        if (toClear.has(`${mi}:${bi}`)) {
          needsClone = true
          break
        }
      }

      if (!needsClone) {
        result.push(msg)
        continue
      }

      // Clone the message with cleared tool results
      const newBlocks: ContentBlock[] = msg.content.map((block, bi) => {
        if (toClear.has(`${mi}:${bi}`) && block.type === 'tool_result') {
          return {
            type: 'tool_result' as const,
            tool_use_id: block.tool_use_id,
            content: '[Old tool result content cleared]',
            is_error: block.is_error,
          }
        }
        return block
      })

      result.push({ role: msg.role, content: newBlocks })
    }

    return result
  },
}

export const ALL_STRATEGIES = [pruneStrategy, truncateStrategy, summarizeStrategy]
