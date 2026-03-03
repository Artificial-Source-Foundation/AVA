import type { ChatMessage } from '@ava/core-v2/llm'

import type { HistoryProcessor } from './types.js'

export interface LastNObservationOptions {
  keepRecent: number
  maxObservationChars: number
}

const DEFAULTS: LastNObservationOptions = {
  keepRecent: 3,
  maxObservationChars: 100_000,
}

export function createLastNObservationsProcessor(
  options: Partial<LastNObservationOptions> = {}
): HistoryProcessor {
  const cfg: LastNObservationOptions = { ...DEFAULTS, ...options }

  return (messages: ChatMessage[]): ChatMessage[] => {
    const resultIndexes: number[] = []
    for (let i = 0; i < messages.length; i++) {
      const message = messages[i]
      if (!message || typeof message.content === 'string') continue
      if (message.content.some((b) => b.type === 'tool_result')) resultIndexes.push(i)
    }

    const keepSet =
      cfg.keepRecent <= 0 ? new Set<number>() : new Set(resultIndexes.slice(-cfg.keepRecent))
    return messages.map((message, index) => {
      if (typeof message.content === 'string') return message
      if (keepSet.has(index)) return message

      const transformed = message.content.map((block) => {
        if (block.type !== 'tool_result') return block
        const lineCount = block.content.length === 0 ? 0 : block.content.split('\n').length
        const clipped = block.content.length > cfg.maxObservationChars
        return {
          ...block,
          content: clipped
            ? `(${lineCount} lines omitted, response clipped)`
            : `(${lineCount} lines omitted)`,
        }
      })
      return { ...message, content: transformed }
    })
  }
}
