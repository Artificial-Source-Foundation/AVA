import type { BusMessage, MessageBus } from '@ava/core-v2/bus'
import { StreamingFuzzyMatcher } from './streaming-fuzzy-matcher.js'

export interface StreamingEditMessage extends BusMessage {
  type: 'edit:stream-diff'
  path?: string
  oldText: string
  newText: string
  startOffset: number
  endOffset: number
  preview: string
}

export interface StreamingEditPreviewMessage extends BusMessage {
  type: 'edit:stream-preview'
  path?: string
  oldText: string
  startOffset: number
  endOffset: number
  confidence: number
}

interface StreamingEditPartial {
  path?: string
  edits?: Array<{ old_text?: string; new_text?: string }>
}

function findFuzzyWindow(
  content: string,
  oldText: string
): { start: number; end: number; confidence: number } | null {
  const exactIndex = content.indexOf(oldText)
  if (exactIndex >= 0) {
    return { start: exactIndex, end: exactIndex + oldText.length, confidence: 1 }
  }

  const matcher = new StreamingFuzzyMatcher(content, 0.8)
  matcher.pushChunk(`${oldText}\n`)
  const best = matcher.getBestMatch()
  if (!best) {
    return null
  }

  return {
    start: best.startOffset,
    end: best.endOffset,
    confidence: best.confidence,
  }
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

      const previewMessage: StreamingEditPreviewMessage = {
        type: 'edit:stream-preview',
        correlationId: this.correlationId,
        timestamp: Date.now(),
        path: partial.path,
        oldText,
        startOffset: range.start,
        endOffset: range.end,
        confidence: range.confidence,
      }
      this.bus.publish(previewMessage)

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
