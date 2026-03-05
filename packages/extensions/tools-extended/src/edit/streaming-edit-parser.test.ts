import { MessageBus } from '@ava/core-v2/bus'
import { describe, expect, it } from 'vitest'
import { StreamingEditParser } from './streaming-edit-parser'

describe('StreamingEditParser', () => {
  it('applies streamed edit and publishes diff events', () => {
    const bus = new MessageBus()
    const parser = new StreamingEditParser(bus, 'corr-1')
    const events: Array<{ type: string }> = []

    bus.subscribe('edit:stream-diff', (message) => {
      events.push(message as { type: string })
    })

    const content = 'const answer = 41\n'
    const partial = {
      edits: [{ old_text: 'const answer = 41', new_text: 'const answer = 42' }],
    }

    const updated = parser.pushPartial(partial, content)

    expect(updated).toContain('42')
    expect(events.length).toBeGreaterThan(0)
  })

  it('does not apply when fuzzy match score is below threshold', () => {
    const bus = new MessageBus()
    const parser = new StreamingEditParser(bus, 'corr-2')

    const content = 'const answer = 41\n'
    const partial = {
      edits: [{ old_text: 'totally different text', new_text: 'const answer = 42' }],
    }

    const updated = parser.pushPartial(partial, content)

    expect(updated).toBe(content)
  })

  it('parses raw partial JSON chunks and applies once valid', () => {
    const bus = new MessageBus()
    const parser = new StreamingEditParser(bus, 'corr-3')
    const content = 'let count = 1\n'

    const unchanged = parser.pushChunk('{"edits":[{"old_text":"let count = 1"', content)
    expect(unchanged).toBe(content)

    const updated = parser.pushChunk(',"new_text":"let count = 2"}]}', content)
    expect(updated).toContain('let count = 2')
  })
})
