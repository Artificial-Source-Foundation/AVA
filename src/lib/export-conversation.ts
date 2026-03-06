/**
 * Conversation Export
 * Converts a session's messages into a Markdown file and triggers download.
 * Supports redaction options, session metadata, and artifact summaries.
 */

import type { Message } from '../types'
import { applyRedaction, type RedactionOptions } from './export-redaction'

// Re-export types so consumers don't need to change imports
export type { RedactionOptions } from './export-redaction'

// ============================================================================
// Types
// ============================================================================

export interface ExportOptions {
  redaction: RedactionOptions
  includeMetadata: boolean
  includeArtifacts: boolean
}

export const DEFAULT_EXPORT_OPTIONS: ExportOptions = {
  redaction: { stripApiKeys: true, stripFilePaths: false, stripEmails: false },
  includeMetadata: true,
  includeArtifacts: true,
}

interface ArtifactSummary {
  created: string[]
  modified: string[]
  deleted: string[]
}

interface SessionMetadata {
  sessionName?: string
  duration: string
  totalCost: number
  toolsUsed: string[]
  messageCount: number
  modelCount: number
}

// ============================================================================
// Artifact Extraction
// ============================================================================

function extractArtifacts(messages: Message[]): ArtifactSummary {
  const created = new Set<string>()
  const modified = new Set<string>()
  const deleted = new Set<string>()

  for (const msg of messages) {
    if (!msg.toolCalls?.length) continue
    for (const tc of msg.toolCalls) {
      const filePath = tc.filePath || (tc.args?.file_path as string) || (tc.args?.path as string)
      if (!filePath) continue
      switch (tc.name) {
        case 'create_file':
        case 'write_file':
          if (tc.name === 'create_file' || tc.args?.isNew) created.add(filePath)
          else modified.add(filePath)
          break
        case 'edit':
        case 'multiedit':
        case 'apply_patch':
          modified.add(filePath)
          break
        case 'delete_file':
          deleted.add(filePath)
          break
      }
    }
  }

  return {
    created: [...created],
    modified: [...modified].filter((f) => !created.has(f)),
    deleted: [...deleted],
  }
}

// ============================================================================
// Metadata
// ============================================================================

function computeMetadata(messages: Message[], sessionName?: string): SessionMetadata {
  const toolSet = new Set<string>()
  const modelSet = new Set<string>()
  let totalCost = 0

  for (const msg of messages) {
    if (msg.model) modelSet.add(msg.model)
    if (msg.costUSD) totalCost += msg.costUSD
    if (msg.toolCalls) {
      for (const tc of msg.toolCalls) toolSet.add(tc.name)
    }
  }

  const first = messages[0]?.createdAt ?? Date.now()
  const last = messages[messages.length - 1]?.createdAt ?? Date.now()
  const durationMs = last - first
  const minutes = Math.floor(durationMs / 60_000)
  const hours = Math.floor(minutes / 60)
  const duration = hours > 0 ? `${hours}h ${minutes % 60}m` : minutes > 0 ? `${minutes}m` : '<1m'

  return {
    sessionName,
    duration,
    totalCost,
    toolsUsed: [...toolSet].sort(),
    messageCount: messages.length,
    modelCount: modelSet.size,
  }
}

// ============================================================================
// Formatting
// ============================================================================

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

function formatMetadataHeader(meta: SessionMetadata): string {
  const lines = ['## Session Info\n', '| Field | Value |', '| --- | --- |']
  lines.push(`| Messages | ${meta.messageCount} |`)
  lines.push(`| Duration | ${meta.duration} |`)
  lines.push(`| Models used | ${meta.modelCount} |`)
  if (meta.totalCost > 0) lines.push(`| Total cost | $${meta.totalCost.toFixed(4)} |`)
  if (meta.toolsUsed.length > 0) lines.push(`| Tools used | ${meta.toolsUsed.join(', ')} |`)
  lines.push('')
  return lines.join('\n')
}

function formatArtifacts(artifacts: ArtifactSummary): string {
  const hasAny =
    artifacts.created.length > 0 || artifacts.modified.length > 0 || artifacts.deleted.length > 0
  if (!hasAny) return ''

  const lines: string[] = ['## Artifacts\n']
  if (artifacts.created.length > 0) {
    lines.push('**Created:**')
    for (const f of artifacts.created) lines.push(`- \`${f}\``)
    lines.push('')
  }
  if (artifacts.modified.length > 0) {
    lines.push('**Modified:**')
    for (const f of artifacts.modified) lines.push(`- \`${f}\``)
    lines.push('')
  }
  if (artifacts.deleted.length > 0) {
    lines.push('**Deleted:**')
    for (const f of artifacts.deleted) lines.push(`- \`${f}\``)
    lines.push('')
  }
  return lines.join('\n')
}

// ============================================================================
// Public API
// ============================================================================

/** Convert messages to a Markdown string with optional enhancements. */
export function messagesToMarkdown(
  messages: Message[],
  sessionName?: string,
  options?: ExportOptions
): string {
  const opts = options ?? DEFAULT_EXPORT_OPTIONS
  const parts: string[] = []

  parts.push(`# ${sessionName || 'Conversation'}`)
  parts.push(`\n*Exported ${new Date().toLocaleString()}*\n`)

  if (opts.includeMetadata) parts.push(formatMetadataHeader(computeMetadata(messages, sessionName)))
  if (opts.includeArtifacts) {
    const section = formatArtifacts(extractArtifacts(messages))
    if (section) parts.push(section)
  }

  parts.push('---\n')

  for (const msg of messages) {
    const role = msg.role === 'user' ? 'You' : msg.role === 'assistant' ? 'Assistant' : 'System'
    const time = formatTimestamp(msg.createdAt)
    parts.push(`### ${role}`)
    parts.push(`*${time}*${msg.model ? ` — ${msg.model}` : ''}\n`)

    const thinking = msg.metadata?.thinking as string | undefined
    if (thinking) {
      parts.push('<details>\n<summary>Thinking</summary>\n')
      parts.push(applyRedaction(thinking, opts.redaction))
      parts.push('\n</details>\n')
    }

    parts.push(applyRedaction(msg.content || '*No content*', opts.redaction))
    parts.push(formatToolCalls(msg))
    parts.push('\n---\n')
  }

  return parts.join('\n')
}

/** Export conversation as a downloadable .md file. */
export function exportConversation(
  messages: Message[],
  sessionName?: string,
  options?: ExportOptions
): void {
  const md = messagesToMarkdown(messages, sessionName, options)
  const blob = new Blob([md], { type: 'text/markdown' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `${(sessionName || 'conversation').replace(/[^a-zA-Z0-9-_ ]/g, '')}-${new Date().toISOString().slice(0, 10)}.md`
  a.click()
  URL.revokeObjectURL(url)
}
