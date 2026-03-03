import type { ContextStrategy } from '@ava/core-v2/extensions'
import type { ChatMessage, ContentBlock } from '@ava/core-v2/llm'

import { buildToolUseIdMap, estimateTokens, isProtectedToolResult } from './common.js'

export interface ObservationMaskingOptions {
  maxObservationTokens: number
  protectedRecentMessages: number
}

const DEFAULT_OPTIONS: ObservationMaskingOptions = {
  maxObservationTokens: 1_500,
  protectedRecentMessages: 12,
}

export function createObservationMaskingStrategy(
  options: Partial<ObservationMaskingOptions> = {}
): ContextStrategy {
  const cfg: ObservationMaskingOptions = { ...DEFAULT_OPTIONS, ...options }

  return {
    name: 'observation-masking',
    description: 'Mask large old tool outputs while keeping structure',
    compact(messages: ChatMessage[]): ChatMessage[] {
      if (messages.length === 0) return []

      const protectedStart = Math.max(0, messages.length - cfg.protectedRecentMessages)
      const toolMap = buildToolUseIdMap(messages)

      return messages.map((message, idx) => {
        if (idx >= protectedStart) return message
        if (typeof message.content === 'string') return message

        const content: ContentBlock[] = message.content.map((block) => {
          if (block.type !== 'tool_result') return block
          if (isProtectedToolResult(block.tool_use_id, toolMap)) return block

          const tokens = estimateTokens(block.content)
          if (tokens <= cfg.maxObservationTokens) return block

          return {
            ...block,
            content: `[Tool output masked - originally ${tokens} tokens]`,
          }
        })

        return { ...message, content }
      })
    },
  }
}

export const observationMaskingStrategy = createObservationMaskingStrategy()
