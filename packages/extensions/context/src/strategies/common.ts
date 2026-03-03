import type { ChatMessage, ContentBlock, MessageContent } from '@ava/core-v2/llm'

export const PRUNE_TOKEN_BUDGET = 40_000
export const PRUNE_MIN_THRESHOLD = 30_000
export const PROTECTED_TOOLS = new Set(['load_skill', 'skill', 'memory_read'])

export function estimateTokens(text: string): number {
  return Math.ceil(text.length / 4)
}

export function contentLength(content: MessageContent): number {
  if (typeof content === 'string') return content.length
  return content.reduce((sum, b) => {
    if (b.type === 'text') return sum + b.text.length
    if (b.type === 'tool_result') return sum + b.content.length
    if (b.type === 'image') return sum + 1000
    return sum + b.name.length + JSON.stringify(b.input).length
  }, 0)
}

export function buildToolUseIdMap(messages: ChatMessage[]): Map<string, string> {
  const map = new Map<string, string>()
  for (const msg of messages) {
    if (typeof msg.content === 'string') continue
    for (const block of msg.content) {
      if (block.type === 'tool_use') map.set(block.id, block.name)
    }
  }
  return map
}

export function isProtectedToolResult(
  toolUseId: string,
  toolNameMap: Map<string, string>
): boolean {
  const name = toolNameMap.get(toolUseId)
  return name !== undefined && PROTECTED_TOOLS.has(name)
}

export function hasToolUse(msg: ChatMessage): boolean {
  if (typeof msg.content === 'string') return false
  return msg.content.some((b) => b.type === 'tool_use')
}

export function hasToolResult(msg: ChatMessage): boolean {
  if (typeof msg.content === 'string') return false
  return msg.content.some((b) => b.type === 'tool_result')
}

export function cloneMessageWithBlocks(message: ChatMessage, blocks: ContentBlock[]): ChatMessage {
  return { role: message.role, content: blocks }
}
