/**
 * Provider message transforms.
 *
 * Reusable transforms for cleaning up ChatMessages before sending to LLM APIs.
 * Useful for providers that reject empty content or non-alternating roles.
 */

import type { ChatMessage, ContentBlock, TextBlock } from '@ava/core-v2/llm'

/** Extract plain text from MessageContent. */
function extractText(content: ChatMessage['content']): string {
  if (typeof content === 'string') return content
  return content
    .filter((b): b is TextBlock => b.type === 'text')
    .map((b) => b.text)
    .join('\n')
}

/**
 * Remove messages with empty or null content.
 * Some providers (Mistral, Cohere) reject messages with empty content blocks.
 */
export function filterEmptyContentBlocks(messages: ChatMessage[]): ChatMessage[] {
  return messages.filter((msg) => {
    if (msg.content === null || msg.content === undefined) return false
    if (typeof msg.content === 'string') return msg.content.length > 0
    return msg.content.length > 0
  })
}

/**
 * Merge consecutive same-role messages into one.
 * Some providers require strictly alternating user/assistant roles.
 * Text blocks are joined with newlines; non-text blocks are preserved.
 */
export function enforceAlternatingRoles(messages: ChatMessage[]): ChatMessage[] {
  if (messages.length === 0) return []

  const result: ChatMessage[] = []

  for (const msg of messages) {
    const prev = result[result.length - 1]

    if (prev && prev.role === msg.role) {
      // Merge into previous message
      const prevText = extractText(prev.content)
      const curText = extractText(msg.content)
      const merged = [prevText, curText].filter((t) => t.length > 0).join('\n')

      // Preserve non-text blocks from both messages
      if (typeof prev.content === 'string' && typeof msg.content === 'string') {
        prev.content = merged
      } else {
        const prevBlocks =
          typeof prev.content === 'string'
            ? [{ type: 'text' as const, text: prev.content }]
            : prev.content
        const curBlocks =
          typeof msg.content === 'string'
            ? [{ type: 'text' as const, text: msg.content }]
            : msg.content

        // Keep all non-text blocks, merge text blocks
        const nonTextBlocks = [
          ...prevBlocks.filter((b) => b.type !== 'text'),
          ...curBlocks.filter((b) => b.type !== 'text'),
        ]
        const textContent: ContentBlock[] =
          merged.length > 0 ? [{ type: 'text' as const, text: merged }] : []

        prev.content = [...textContent, ...nonTextBlocks]
      }
    } else {
      // Clone to avoid mutating the original
      result.push({
        role: msg.role,
        content: typeof msg.content === 'string' ? msg.content : [...msg.content],
      })
    }
  }

  return result
}
