import type { BusMessage, MessageBus } from '@ava/core-v2/bus'
import { normalizeForMatch } from './normalize-for-match.js'

export interface StreamingEditMessage extends BusMessage {
  type: 'edit:stream-diff'
  path?: string
  oldText: string
  newText: string
  startOffset: number
  endOffset: number
  preview: string
}

interface StreamingEditPartial {
  path?: string
  edits?: Array<{ old_text?: string; new_text?: string }>
}

function similarity(left: string, right: string): number {
  const a = normalizeForMatch(left)
  const b = normalizeForMatch(right)
  if (a === b) {
    return 1
  }
  const maxLength = Math.max(a.length, b.length)
  if (maxLength === 0) {
    return 1
  }

  const limit = Math.min(a.length, b.length)
  let same = 0
  for (let i = 0; i < limit; i += 1) {
    if (a[i] === b[i]) {
      same += 1
    }
  }

  return same / maxLength
}

function findFuzzyWindow(content: string, oldText: string): { start: number; end: number } | null {
  const exactIndex = content.indexOf(oldText)
  if (exactIndex >= 0) {
    return { start: exactIndex, end: exactIndex + oldText.length }
  }

  const lines = content.split('\n')
  const oldLines = oldText.split('\n')
  if (oldLines.length === 0) {
    return null
  }

  let bestStart = -1
  let bestScore = 0
  const width = oldLines.length

  for (let i = 0; i <= lines.length - width; i += 1) {
    const candidate = lines.slice(i, i + width).join('\n')
    const score = similarity(candidate, oldText)
    if (score > bestScore) {
      bestScore = score
      bestStart = i
    }
  }

  if (bestStart < 0 || bestScore < 0.8) {
    return null
  }

  const prefix = content.split('\n').slice(0, bestStart).join('\n')
  const start = prefix.length > 0 ? prefix.length + 1 : 0
  const match = content
    .split('\n')
    .slice(bestStart, bestStart + width)
    .join('\n')
  return { start, end: start + match.length }
}

export class StreamingEditParser {
  private readonly bus: MessageBus
  private readonly correlationId: string
  private rawBuffer = ''
  private lastApplied = new Set<string>()

  constructor(bus: MessageBus, correlationId: string) {
    this.bus = bus
    this.correlationId = correlationId
  }

  pushChunk(chunk: string, content: string): string {
    this.rawBuffer += chunk
    const parsed = this.tryParsePartial(this.rawBuffer)
    if (!parsed) {
      return content
    }

    return this.pushPartial(parsed, content)
  }

  pushPartial(partial: StreamingEditPartial, content: string): string {
    const edits = partial.edits ?? []
    let current = content

    for (let index = 0; index < edits.length; index += 1) {
      const edit = edits[index]
      const oldText = edit?.old_text
      const newText = edit?.new_text
      if (!oldText || !newText) {
        continue
      }

      const fingerprint = `${index}:${oldText}:${newText}`
      if (this.lastApplied.has(fingerprint)) {
        continue
      }

      const range = findFuzzyWindow(current, oldText)
      if (!range) {
        continue
      }

      const before = current.slice(0, range.start)
      const after = current.slice(range.end)
      current = `${before}${newText}${after}`
      this.lastApplied.add(fingerprint)

      const message: StreamingEditMessage = {
        type: 'edit:stream-diff',
        correlationId: this.correlationId,
        timestamp: Date.now(),
        path: partial.path,
        oldText,
        newText,
        startOffset: range.start,
        endOffset: range.end,
        preview: current.slice(
          Math.max(0, range.start - 40),
          Math.min(current.length, range.start + newText.length + 40)
        ),
      }
      this.bus.publish(message)
    }

    return current
  }

  private tryParsePartial(buffer: string): StreamingEditPartial | null {
    const trimmed = buffer.trim()
    if (trimmed.length === 0) {
      return null
    }

    try {
      return JSON.parse(trimmed) as StreamingEditPartial
    } catch {
      const lastBrace = Math.max(trimmed.lastIndexOf('}'), trimmed.lastIndexOf(']'))
      if (lastBrace < 0) {
        return null
      }

      const candidate = trimmed.slice(0, lastBrace + 1)
      try {
        return JSON.parse(candidate) as StreamingEditPartial
      } catch {
        return null
      }
    }
  }
}
