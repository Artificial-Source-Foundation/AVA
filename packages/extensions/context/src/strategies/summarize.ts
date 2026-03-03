import type { ContextStrategy } from '@ava/core-v2/extensions'
import type { ChatMessage } from '@ava/core-v2/llm'

import { truncateStrategy } from './truncate.js'

export const summarizeStrategy: ContextStrategy = {
  name: 'summarize',
  description: 'Summarize older messages and keep recent context',
  compact(messages: ChatMessage[], targetTokens: number): ChatMessage[] {
    if (messages.length <= 6) return messages

    const keepRecentCount = Math.max(4, Math.floor(messages.length / 3))
    const recent = messages.slice(-keepRecentCount)
    const older = messages.slice(0, -keepRecentCount)
    const assistantCount = older.filter((m) => m.role === 'assistant').length

    const summary: ChatMessage = {
      role: 'system',
      content: `Summary of earlier conversation (${older.length} messages, ${assistantCount} assistant responses). Keep this in mind while continuing with the latest context.`,
    }

    const result = [summary, ...recent]
    const estimatedTokens = Math.ceil(
      result.reduce((sum, m) => sum + JSON.stringify(m).length, 0) / 4
    )
    if (estimatedTokens > targetTokens) {
      return truncateStrategy.compact(result, targetTokens)
    }
    return result
  },
}
