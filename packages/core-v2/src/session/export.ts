/**
 * Session export — convert conversation messages to Markdown or JSON.
 */

import type { ChatMessage, ContentBlock, MessageContent } from '../llm/types.js'

// ─── Helpers ────────────────────────────────────────────────────────────────

function contentToText(content: MessageContent): string {
  if (typeof content === 'string') return content

  return content
    .map((block: ContentBlock) => {
      switch (block.type) {
        case 'text':
          return block.text
        case 'tool_use':
          return `**Tool call: ${block.name}**\n\`\`\`json\n${JSON.stringify(block.input, null, 2)}\n\`\`\``
        case 'tool_result':
          return `**Tool result** (${block.is_error ? 'error' : 'success'}):\n\`\`\`\n${block.content}\n\`\`\``
        case 'image':
          return `[Image: ${block.source.media_type}]`
        default:
          return ''
      }
    })
    .filter(Boolean)
    .join('\n\n')
}

function roleLabel(role: string): string {
  switch (role) {
    case 'user':
      return 'User'
    case 'assistant':
      return 'Assistant'
    case 'system':
      return 'System'
    default:
      return role
  }
}

// ─── Export Functions ────────────────────────────────────────────────────────

/**
 * Convert conversation messages to readable Markdown.
 */
export function exportSessionToMarkdown(messages: ChatMessage[]): string {
  if (messages.length === 0) return '# Session Export\n\n_No messages._\n'

  const lines: string[] = ['# Session Export', '']

  for (const msg of messages) {
    lines.push(`## ${roleLabel(msg.role)}`, '')
    lines.push(contentToText(msg.content))
    lines.push('', '---', '')
  }

  return lines.join('\n')
}

/**
 * Convert conversation messages to pretty-printed JSON.
 */
export function exportSessionToJSON(messages: ChatMessage[]): string {
  return JSON.stringify(messages, null, 2)
}
