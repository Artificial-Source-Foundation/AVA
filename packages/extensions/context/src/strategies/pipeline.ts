import type { ContextStrategy } from '@ava/core-v2/extensions'
import type { ChatMessage } from '@ava/core-v2/llm'

import { estimateTokens } from './common.js'

export function createPipelineStrategy(strategies: ContextStrategy[]): ContextStrategy {
  return {
    name: 'pipeline',
    description: 'Run multiple context strategies in sequence',
    compact(messages: ChatMessage[], targetTokens: number): ChatMessage[] {
      let current = messages
      for (const strategy of strategies) {
        current = strategy.compact(current, targetTokens)
        const totalTokens = current.reduce(
          (sum, msg) => sum + estimateTokens(JSON.stringify(msg)),
          0
        )
        if (totalTokens <= targetTokens) break
      }
      return current
    },
  }
}
