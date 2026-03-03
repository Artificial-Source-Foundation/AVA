import type { ChatMessage } from '@ava/core-v2/llm'

import type { HistoryProcessor } from './types.js'

function buildToolNameMap(messages: ChatMessage[]): Map<string, string> {
  const map = new Map<string, string>()
  for (const message of messages) {
    if (typeof message.content === 'string') continue
    for (const block of message.content) {
      if (block.type === 'tool_use') map.set(block.id, block.name)
    }
  }
  return map
}

export function createTagToolCallsProcessor(): HistoryProcessor {
  return (messages: ChatMessage[]): ChatMessage[] => {
    const nameMap = buildToolNameMap(messages)
    return messages.map((message) => {
      if (typeof message.content === 'string') return message
      return {
        ...message,
        content: message.content.map((block) => {
          if (block.type !== 'tool_result') return block
          const toolName = nameMap.get(block.tool_use_id) ?? 'unknown'
          return {
            ...block,
            content: `<tool_output name="${toolName}">\n${block.content}\n</tool_output>`,
          }
        }),
      }
    })
  }
}
