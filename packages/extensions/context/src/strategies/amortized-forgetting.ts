import type { ContextStrategy } from '@ava/core-v2/extensions'
import type { ChatMessage, ContentBlock } from '@ava/core-v2/llm'

import { estimateTokens } from './common.js'

export interface AmortizedForgettingOptions {
  preserveRecentMessages: number
}

const DEFAULT_OPTIONS: AmortizedForgettingOptions = {
  preserveRecentMessages: 10,
}

export function createAmortizedForgettingStrategy(
  options: Partial<AmortizedForgettingOptions> = {}
): ContextStrategy {
  const cfg: AmortizedForgettingOptions = { ...DEFAULT_OPTIONS, ...options }

  return {
    name: 'amortized-forgetting',
    description: 'Gradually reduce detail on older messages',
    compact(messages: ChatMessage[], targetTokens: number): ChatMessage[] {
      const targetChars = targetTokens * 4
      const totalChars = messages.reduce((sum, msg) => sum + JSON.stringify(msg).length, 0)
      if (totalChars <= targetChars) return messages

      const protectedStart = Math.max(0, messages.length - cfg.preserveRecentMessages)
      return messages.map((message, idx) => {
        if (idx >= protectedStart || typeof message.content === 'string') return message
        const ageFactor = (protectedStart - idx) / Math.max(1, protectedStart)

        const content: ContentBlock[] = message.content.map((block) => {
          if (block.type !== 'tool_result') return block
          const tokens = estimateTokens(block.content)
          if (tokens < 800) return block

          const keepRatio = Math.max(0.1, 1 - ageFactor)
          const keepChars = Math.max(120, Math.floor(block.content.length * keepRatio))
          const truncated = block.content.slice(0, keepChars)
          return {
            ...block,
            content: `${truncated}\n[Older details omitted by amortized forgetting]`,
          }
        })

        return { ...message, content }
      })
    },
  }
}

export const amortizedForgettingStrategy = createAmortizedForgettingStrategy()
