import type { ContextStrategy } from '@ava/core-v2/extensions'
import type { ChatMessage } from '@ava/core-v2/llm'

import { contentLength, hasToolResult, hasToolUse } from './common.js'

export const truncateStrategy: ContextStrategy = {
  name: 'truncate',
  description: 'Keep most recent messages by token budget',
  compact(messages: ChatMessage[], targetTokens: number): ChatMessage[] {
    if (messages.length === 0) return []
    const maxChars = targetTokens * 4
    let total = 0
    const kept: ChatMessage[] = []

    const first = messages[0]
    const system = first && first.role === 'system' ? first : null
    if (system) {
      kept.push(system)
      total += contentLength(system.content)
    }

    for (let i = messages.length - 1; i >= (system ? 1 : 0); i--) {
      const current = messages[i]
      if (!current) continue
      const currentLen = contentLength(current.content)
      const previous = i > (system ? 1 : 0) ? (messages[i - 1] ?? null) : null
      const isPair =
        current.role === 'user' &&
        hasToolResult(current) &&
        previous?.role === 'assistant' &&
        hasToolUse(previous)

      if (isPair && previous) {
        const pairLen = currentLen + contentLength(previous.content)
        if (total + pairLen > maxChars && kept.length > (system ? 1 : 0)) break
        kept.splice(system ? 1 : 0, 0, previous, current)
        total += pairLen
        i--
        continue
      }

      if (total + currentLen > maxChars && kept.length > (system ? 1 : 0)) break
      kept.splice(system ? 1 : 0, 0, current)
      total += currentLen
    }

    return kept
  },
}
