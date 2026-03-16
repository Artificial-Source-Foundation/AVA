/**
 * History Builder
 * Converts session Messages to ChatMessage[] with proper tool_result blocks.
 *
 * This is the critical fix for multi-turn memory: the LLM API requires that
 * every tool_use block in an assistant message be followed by corresponding
 * tool_result blocks in a user message. Without this, the LLM sees tool calls
 * with no results and loses conversation context.
 */

import type { Message } from '../../types'

/** Local types (replaces @ava/core-v2/llm import) */
interface ContentBlock {
  type: string
  text?: string
  id?: string
  name?: string
  input?: unknown
  tool_use_id?: string
  content?: string
  is_error?: boolean
  [key: string]: unknown
}

interface ChatMessage {
  role: 'user' | 'assistant' | 'system'
  content: ContentBlock[] | string
}

/**
 * Build structured conversation history from session messages.
 * For each assistant message with toolCalls, emits:
 *   1. assistant message with text + tool_use blocks
 *   2. user message with tool_result blocks (output from each tool call)
 *
 * @param messages - Session messages to convert
 * @param excludeIds - Message IDs to skip (e.g., current user + assistant placeholders)
 */
export function buildConversationHistory(
  messages: Message[],
  excludeIds?: Set<string>
): ChatMessage[] {
  const result: ChatMessage[] = []

  for (const m of messages) {
    if (excludeIds?.has(m.id)) continue

    const role = m.role === 'system' ? 'user' : m.role

    if (m.role === 'assistant' && m.toolCalls?.length) {
      // 1. Assistant message with text + tool_use blocks
      const blocks: ContentBlock[] = []
      if (m.content) blocks.push({ type: 'text', text: m.content })
      for (const tc of m.toolCalls) {
        blocks.push({
          type: 'tool_use',
          id: tc.id,
          name: tc.name,
          input: tc.args ?? {},
        })
      }
      result.push({ role: 'assistant', content: blocks })

      // 2. User message with tool_result blocks for each tool call
      const resultBlocks: ContentBlock[] = []
      for (const tc of m.toolCalls) {
        const output = tc.output ?? tc.error ?? ''
        resultBlocks.push({
          type: 'tool_result',
          tool_use_id: tc.id,
          content: output
            ? output.length > 4000
              ? `${output.slice(0, 4000)}... [truncated]`
              : output
            : tc.status === 'success'
              ? '(success, no output)'
              : '(no output)',
          is_error: tc.status === 'error',
        })
      }
      if (resultBlocks.length > 0) {
        result.push({ role: 'user', content: resultBlocks })
      }
    } else if (m.content) {
      // Plain text message — truncate very long content
      const content =
        m.content.length > 4000 ? `${m.content.slice(0, 4000)}... [truncated]` : m.content
      result.push({ role, content })
    }
  }

  return result
}
