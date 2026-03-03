import type { ContextStrategy } from '@ava/core-v2/extensions'
import type { ChatMessage } from '@ava/core-v2/llm'

export const DEFAULT_WINDOW_MESSAGES = 12

export function createSlidingWindowStrategy(
  windowMessages = DEFAULT_WINDOW_MESSAGES
): ContextStrategy {
  return {
    name: 'sliding-window',
    description: 'Keep only the most recent messages',
    compact(messages: ChatMessage[]): ChatMessage[] {
      if (messages.length <= windowMessages) return messages

      const first = messages[0]
      const system = first && first.role === 'system' ? first : null
      const start = Math.max(system ? 1 : 0, messages.length - windowMessages)
      const recent = messages.slice(start)

      if (!system) return recent
      return [system, ...recent]
    },
  }
}

export const slidingWindowStrategy = createSlidingWindowStrategy()
