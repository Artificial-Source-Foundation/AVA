import { describe, expect, it } from 'vitest'
import { parseSSELines, readSSEStream } from './sse.js'

describe('parseSSELines', () => {
  it('extracts data from SSE lines', () => {
    const lines = ['data: {"text":"hello"}', 'data: {"text":"world"}']
    const results = [...parseSSELines(lines)]
    expect(results).toHaveLength(2)
    expect(results[0]!.data).toBe('{"text":"hello"}')
    expect(results[1]!.data).toBe('{"text":"world"}')
  })

  it('detects [DONE] sentinel', () => {
    const results = [...parseSSELines(['data: [DONE]'])]
    expect(results).toHaveLength(1)
    expect(results[0]!.done).toBe(true)
    expect(results[0]!.data).toBe('')
  })

  it('skips event lines', () => {
    const lines = ['event: message_start', 'data: {"type":"start"}']
    const results = [...parseSSELines(lines)]
    expect(results).toHaveLength(1)
    expect(results[0]!.data).toBe('{"type":"start"}')
  })

  it('skips empty data', () => {
    const results = [...parseSSELines(['data: '])]
    expect(results).toHaveLength(0)
  })

  it('skips comment lines', () => {
    const results = [...parseSSELines([': comment', 'data: hello'])]
    expect(results).toHaveLength(1)
  })

  it('skips empty lines', () => {
    const results = [...parseSSELines(['', '  ', 'data: hello'])]
    expect(results).toHaveLength(1)
  })
})

describe('readSSEStream', () => {
  function makeReader(chunks: string[]): ReadableStreamDefaultReader<Uint8Array> {
    const encoder = new TextEncoder()
    let index = 0
    return {
      read: async () => {
        if (index >= chunks.length) return { done: true, value: undefined }
        const value = encoder.encode(chunks[index])
        index++
        return { done: false, value }
      },
      releaseLock: () => {},
      cancel: async () => {},
      closed: Promise.resolve(undefined),
    } as unknown as ReadableStreamDefaultReader<Uint8Array>
  }

  it('yields data from complete SSE lines', async () => {
    const reader = makeReader(['data: hello\ndata: world\n'])
    const all: string[][] = []
    for await (const batch of readSSEStream(reader)) {
      all.push(batch)
    }
    expect(all).toHaveLength(1)
    expect(all[0]).toEqual(['hello', 'world'])
  })

  it('handles partial lines across chunks', async () => {
    const reader = makeReader(['data: hel', 'lo\ndata: world\n'])
    const all: string[][] = []
    for await (const batch of readSSEStream(reader)) {
      all.push(batch)
    }
    // First chunk yields nothing (partial), second yields both
    expect(all.flat()).toContain('hello')
    expect(all.flat()).toContain('world')
  })

  it('yields empty for stream with no data lines', async () => {
    const reader = makeReader(['event: start\n\n'])
    const all: string[][] = []
    for await (const batch of readSSEStream(reader)) {
      all.push(batch)
    }
    expect(all).toHaveLength(0)
  })

  it('handles empty stream', async () => {
    const reader = makeReader([])
    const all: string[][] = []
    for await (const batch of readSSEStream(reader)) {
      all.push(batch)
    }
    expect(all).toHaveLength(0)
  })

  it('skips [DONE] sentinel in data lines', async () => {
    const reader = makeReader(['data: hello\ndata: [DONE]\n'])
    const all: string[][] = []
    for await (const batch of readSSEStream(reader)) {
      all.push(batch)
    }
    expect(all.flat()).toEqual(['hello'])
  })
})
