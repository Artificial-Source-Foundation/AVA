/**
 * Message Segmentation
 *
 * Splits an assistant message into alternating text + tool segments
 * for Goose-style inline rendering (tools appear at the point they were used).
 */

import type { ToolCall } from '../../types'

export type MessageSegment =
  | { type: 'text'; content: string }
  | { type: 'tools'; toolCalls: ToolCall[] }

/**
 * Split message content + tool calls into ordered segments.
 * Uses contentOffset on each ToolCall to place tools inline.
 *
 * Falls back to [tools, text] if no contentOffset data is present
 * (legacy messages created before this feature).
 */
export function segmentMessage(content: string, toolCalls?: ToolCall[]): MessageSegment[] {
  // No tool calls → single text segment
  if (!toolCalls || toolCalls.length === 0) {
    return content ? [{ type: 'text', content }] : []
  }

  // If no contentOffset data, fall back to: tools then text
  const hasOffsets = toolCalls.some((tc) => tc.contentOffset !== undefined)
  if (!hasOffsets) {
    const segments: MessageSegment[] = [{ type: 'tools', toolCalls }]
    if (content) segments.push({ type: 'text', content })
    return segments
  }

  // Sort tool calls by contentOffset, then startedAt for ties
  const sorted = [...toolCalls].sort((a, b) => {
    const offsetA = a.contentOffset ?? 0
    const offsetB = b.contentOffset ?? 0
    return offsetA !== offsetB ? offsetA - offsetB : a.startedAt - b.startedAt
  })

  const segments: MessageSegment[] = []
  let lastOffset = 0

  // Walk through sorted tools, inserting text segments between groups
  let i = 0
  while (i < sorted.length) {
    const offset = sorted[i]!.contentOffset ?? 0

    // Text before this group of tools
    if (offset > lastOffset && content) {
      const text = content.slice(lastOffset, offset)
      if (text.trim()) segments.push({ type: 'text', content: text })
    }

    // Collect consecutive tools at the same offset
    const group: ToolCall[] = []
    while (i < sorted.length && (sorted[i]!.contentOffset ?? 0) === offset) {
      group.push(sorted[i]!)
      i++
    }
    segments.push({ type: 'tools', toolCalls: group })
    lastOffset = offset
  }

  // Remaining text after last tool group
  if (lastOffset < content.length && content) {
    const remaining = content.slice(lastOffset)
    if (remaining.trim()) segments.push({ type: 'text', content: remaining })
  }

  // Edge case: tools exist but no text content at all → just tools
  if (segments.length === 0) {
    segments.push({ type: 'tools', toolCalls })
  }

  return segments
}
