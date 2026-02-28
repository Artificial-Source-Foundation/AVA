/**
 * Conversation Export
 * Converts a session's messages into a Markdown file and triggers download.
 */

import type { Message } from '../types'

function formatTimestamp(ts: number): string {
  return new Date(ts).toLocaleString()
}

function formatToolCalls(message: Message): string {
  if (!message.toolCalls?.length) return ''
  const lines: string[] = []
  for (const tc of message.toolCalls) {
    const status = tc.status === 'error' ? 'failed' : tc.status
    lines.push(`- **${tc.name}** (${status})`)
    if (tc.filePath) lines.push(`  - File: \`${tc.filePath}\``)
    if (tc.error) lines.push(`  - Error: ${tc.error}`)
  }
  return `\n\n<details>\n<summary>Tool calls (${message.toolCalls.length})</summary>\n\n${lines.join('\n')}\n</details>`
}

/**
 * Convert messages to a Markdown string.
 */
export function messagesToMarkdown(messages: Message[], sessionName?: string): string {
  const parts: string[] = []

  parts.push(`# ${sessionName || 'Conversation'}`)
  parts.push(`\n*Exported ${new Date().toLocaleString()}*\n`)
  parts.push('---\n')

  for (const msg of messages) {
    const role = msg.role === 'user' ? 'You' : msg.role === 'assistant' ? 'Assistant' : 'System'
    const time = formatTimestamp(msg.createdAt)

    parts.push(`### ${role}`)
    parts.push(`*${time}*${msg.model ? ` — ${msg.model}` : ''}\n`)

    // Thinking block
    const thinking = msg.metadata?.thinking as string | undefined
    if (thinking) {
      parts.push('<details>\n<summary>Thinking</summary>\n')
      parts.push(thinking)
      parts.push('\n</details>\n')
    }

    parts.push(msg.content || '*No content*')
    parts.push(formatToolCalls(msg))
    parts.push('\n---\n')
  }

  return parts.join('\n')
}

/**
 * Export conversation as a downloadable .md file.
 */
export function exportConversation(messages: Message[], sessionName?: string): void {
  const md = messagesToMarkdown(messages, sessionName)
  const blob = new Blob([md], { type: 'text/markdown' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `${(sessionName || 'conversation').replace(/[^a-zA-Z0-9-_ ]/g, '')}-${new Date().toISOString().slice(0, 10)}.md`
  a.click()
  URL.revokeObjectURL(url)
}
