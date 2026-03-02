/**
 * Anthropic Prompt Caching
 *
 * Adds `cache_control: { type: 'ephemeral' }` markers to messages
 * for cost reduction via Anthropic's prompt caching.
 * https://docs.anthropic.com/en/docs/build-with-claude/prompt-caching
 */

import type { ChatMessage, ContentBlock, MessageContent } from '@ava/core-v2/llm'

interface CacheControl {
  type: 'ephemeral'
}

interface CacheableTextBlock {
  type: 'text'
  text: string
  cache_control?: CacheControl
}

type CacheableContentBlock = (ContentBlock | CacheableTextBlock) & {
  cache_control?: CacheControl
}

export type CacheableMessageContent = string | CacheableContentBlock[]

export interface CacheableChatMessage {
  role: 'user' | 'assistant' | 'system'
  content: CacheableMessageContent
}

/**
 * Mark a content block with cache_control.
 * If content is a string, wraps it in a text block first.
 */
function markContent(content: MessageContent): CacheableContentBlock[] {
  if (typeof content === 'string') {
    return [{ type: 'text', text: content, cache_control: { type: 'ephemeral' } }]
  }

  if (content.length === 0) return []

  // Clone blocks and mark the last one
  const blocks: CacheableContentBlock[] = content.map((b) => ({ ...b }))
  const last = blocks[blocks.length - 1]!
  last.cache_control = { type: 'ephemeral' }
  return blocks
}

/**
 * Add cache_control markers to messages for Anthropic prompt caching.
 *
 * Strategy:
 * - Mark system message with cache_control (cached across turns)
 * - Mark last 2 user messages with cache_control (sliding window)
 *
 * Returns a new array — does not mutate the input.
 */
export function addCacheControlMarkers(messages: ChatMessage[]): CacheableChatMessage[] {
  if (messages.length === 0) return []

  const result: CacheableChatMessage[] = messages.map((m) => ({
    role: m.role,
    content: typeof m.content === 'string' ? m.content : m.content.map((b) => ({ ...b })),
  }))

  // Mark system message
  for (const msg of result) {
    if (msg.role === 'system') {
      msg.content = markContent(msg.content)
      break
    }
  }

  // Find last 2 user messages and mark them
  const userIndices: number[] = []
  for (let i = result.length - 1; i >= 0; i--) {
    if (result[i]!.role === 'user') {
      userIndices.push(i)
      if (userIndices.length === 2) break
    }
  }

  for (const idx of userIndices) {
    result[idx]!.content = markContent(result[idx]!.content)
  }

  return result
}
