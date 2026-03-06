import type { ChatMessage, ContentBlock, ToolResultBlock, ToolUseBlock } from '@ava/core-v2/llm'

function providerSupportsThinking(provider: string): boolean {
  return provider === 'anthropic'
}

function normalizeToolCallId(id: string, provider: string): string {
  const desiredPrefix = provider === 'anthropic' ? 'toolu_' : 'call_'
  if (id.startsWith(desiredPrefix)) return id

  const stripped = id.replace(/^(call_|toolu_)/, '')
  const safe = stripped.replace(/[^a-zA-Z0-9_-]/g, '_') || 'auto'
  return `${desiredPrefix}${safe}`
}

function normalizeContent(
  content: unknown,
  provider: string,
  idMap: Map<string, string>
): string | ContentBlock[] {
  if (content == null) return ''
  if (typeof content === 'string') return content
  if (!Array.isArray(content)) return String(content)

  const supportsThinking = providerSupportsThinking(provider)
  const blocks: ContentBlock[] = []

  for (const raw of content) {
    if (!raw || typeof raw !== 'object' || !('type' in raw)) continue
    const block = raw as Record<string, unknown>

    if (block.type === 'thinking' && !supportsThinking) {
      continue
    }

    if (block.type === 'tool_use') {
      const toolUse = block as unknown as ToolUseBlock
      const normalizedId = normalizeToolCallId(toolUse.id, provider)
      idMap.set(toolUse.id, normalizedId)
      blocks.push({ ...toolUse, id: normalizedId })
      continue
    }

    if (block.type === 'tool_result') {
      const toolResult = block as unknown as ToolResultBlock
      const mappedId =
        idMap.get(toolResult.tool_use_id) ?? normalizeToolCallId(toolResult.tool_use_id, provider)
      blocks.push({ ...toolResult, tool_use_id: mappedId, content: toolResult.content ?? '' })
      continue
    }

    if (block.type === 'text') {
      blocks.push({ type: 'text', text: typeof block.text === 'string' ? block.text : '' })
      continue
    }

    blocks.push(block as unknown as ContentBlock)
  }

  return blocks
}

function dropOrphanedToolResults(messages: ChatMessage[]): ChatMessage[] {
  const seenToolUseIds = new Set<string>()

  return messages.map((message) => {
    if (typeof message.content === 'string') return message

    const filtered: ContentBlock[] = []
    for (const block of message.content) {
      if (block.type === 'tool_use') {
        seenToolUseIds.add(block.id)
        filtered.push(block)
        continue
      }

      if (block.type === 'tool_result') {
        if (seenToolUseIds.has(block.tool_use_id)) {
          filtered.push(block)
        }
        continue
      }

      filtered.push(block)
    }

    return filtered.length === message.content.length ? message : { ...message, content: filtered }
  })
}

export function normalizeProviderMessages(
  messages: ChatMessage[],
  provider: string
): ChatMessage[] {
  const idMap = new Map<string, string>()

  const normalized = messages.map((message) => ({
    ...message,
    content: normalizeContent(message.content, provider, idMap),
  }))

  return dropOrphanedToolResults(normalized)
}
