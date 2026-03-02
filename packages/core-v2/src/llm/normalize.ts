/**
 * Cross-provider message normalization.
 *
 * When model/provider changes mid-session, normalize messages generated
 * by Provider A so Provider B can understand them. Handles:
 * - Tool call ID format differences (OpenAI 40+ chars → some providers need shorter)
 * - Thinking block stripping (Anthropic → OpenAI)
 * - System message positioning
 */

import type {
  ChatMessage,
  ContentBlock,
  MessageContent,
  ToolResultBlock,
  ToolUseBlock,
} from './types.js'

const MAX_TOOL_ID_LENGTH = 64

/**
 * Normalize messages for cross-provider compatibility.
 * Safe to call even when provider hasn't changed (returns messages as-is if no changes needed).
 */
export function normalizeMessages(messages: ChatMessage[]): ChatMessage[] {
  return messages.map(normalizeMessage)
}

function normalizeMessage(msg: ChatMessage): ChatMessage {
  if (typeof msg.content === 'string') return msg

  const normalized = msg.content.map(normalizeBlock)
  const hasChanges = normalized.some((b, i) => b !== msg.content[i])
  return hasChanges ? { ...msg, content: normalized } : msg
}

function normalizeBlock(block: ContentBlock): ContentBlock {
  if (block.type === 'tool_use') {
    return normalizeToolUseId(block)
  }
  if (block.type === 'tool_result') {
    return normalizeToolResultId(block)
  }
  return block
}

function normalizeToolUseId(block: ToolUseBlock): ToolUseBlock {
  if (block.id.length <= MAX_TOOL_ID_LENGTH) return block
  return { ...block, id: truncateId(block.id) }
}

function normalizeToolResultId(block: ToolResultBlock): ToolResultBlock {
  if (block.tool_use_id.length <= MAX_TOOL_ID_LENGTH) return block
  return { ...block, tool_use_id: truncateId(block.tool_use_id) }
}

function truncateId(id: string): string {
  return id.slice(0, MAX_TOOL_ID_LENGTH)
}

/**
 * Strip thinking blocks from message content.
 * Useful when switching from Anthropic (which generates thinking) to OpenAI.
 */
export function stripThinkingBlocks(content: MessageContent): MessageContent {
  if (typeof content === 'string') return content
  const filtered = content.filter(
    (b) => !('type' in b && (b as unknown as { type: string }).type === 'thinking')
  )
  return filtered.length === content.length ? content : filtered
}

/**
 * Ensure system messages are positioned correctly for the target provider.
 * OpenAI expects system at index 0. Anthropic uses a separate parameter.
 */
export function normalizeSystemPosition(messages: ChatMessage[]): ChatMessage[] {
  const systemMsgs = messages.filter((m) => m.role === 'system')
  const nonSystem = messages.filter((m) => m.role !== 'system')

  if (systemMsgs.length === 0) return messages
  // Merge all system messages and put at front
  const merged: ChatMessage = {
    role: 'system',
    content: systemMsgs
      .map((m) => (typeof m.content === 'string' ? m.content : ''))
      .filter(Boolean)
      .join('\n\n'),
  }
  return [merged, ...nonSystem]
}
