import type { ChatMessage } from '@ava/core-v2/llm'

import type { HistoryProcessor } from './types.js'

export interface CacheControlOptions {
  markLastMessages: number
}

const DEFAULTS: CacheControlOptions = {
  markLastMessages: 3,
}

export function createCacheControlProcessor(
  provider: string,
  options: Partial<CacheControlOptions> = {}
): HistoryProcessor {
  const cfg: CacheControlOptions = { ...DEFAULTS, ...options }
  if (provider !== 'anthropic') {
    return (messages) => messages
  }

  return (messages: ChatMessage[]): ChatMessage[] => {
    const start = Math.max(0, messages.length - cfg.markLastMessages)
    return messages.map((message, index) => {
      if (index < start || message.role !== 'user') return message
      return {
        ...message,
        _variant: message._variant ?? 'ephemeral',
      }
    })
  }
}
