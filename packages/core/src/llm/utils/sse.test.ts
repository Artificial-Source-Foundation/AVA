import { describe, expect, it } from 'vitest'
import { parseSSELines, readSSEStream } from './sse.js'

describe('parseSSELines', () => {
  it('parses data lines', () => {
    const results = [...parseSSELines(['data: {"text":"hello"}'])]
    expect(results).toEqual([{ data: '{"text":"hello"}', done: false }])
  })

  it('skips event: lines', () => {
    const results = [...parseSSELines(['event: message', 'data: {"text":"hi"}'])]
    expect(results).toHaveLength(1)
    expect(results[0].data).toBe('{"text":"hi"}')
  })

  it('skips empty data', () => {
    const results = [...parseSSELines(['data: ', 'data:  '])]
    expect(results).toHaveLength(0)
  })

  it('skips comment lines', () => {
    const results = [...parseSSELines([': comment', 'data: ok'])]
    expect(results).toHaveLength(1)
    expect(results[0].data).toBe('ok')
  })

  it('handles [DONE] sentinel', () => {
    const results = [...parseSSELines(['data: [DONE]'])]
    expect(results).toEqual([{ data: '', done: true }])
  })

  it('handles multiple lines in order', () => {
    const results = [...parseSSELines(['data: {"id":1}', 'data: {"id":2}', 'data: [DONE]'])]
    expect(results).toHaveLength(3)
    expect(results[0].data).toBe('{"id":1}')
    expect(results[1].data).toBe('{"id":2}')
    expect(results[2].done).toBe(true)
  })

  it('skips lines without data: prefix', () => {
    const results = [...parseSSELines(['no prefix', 'event: something', 'data: valid'])]
    expect(results).toHaveLength(1)
    expect(results[0].data).toBe('valid')
  })
})

describe('readSSEStream', () => {
  function createMockReader(chunks: string[]): ReadableStreamDefaultReader<Uint8Array> {
    const encoder = new TextEncoder()
    let index = 0
    return {
      read: async () => {
        if (index >= chunks.length) {
          return { done: true, value: undefined } as ReadableStreamReadDoneResult
        }
        const value = encoder.encode(chunks[index++])
        return { done: false, value } as ReadableStreamReadValueResult<Uint8Array>
      },
      releaseLock: () => {},
      cancel: async () => {},
      closed: Promise.resolve(undefined),
    } as ReadableStreamDefaultReader<Uint8Array>
  }

  it('yields parsed data from complete lines', async () => {
    const reader = createMockReader(['data: {"a":1}\ndata: {"b":2}\n'])
    const results: string[][] = []
    for await (const batch of readSSEStream(reader)) {
      results.push(batch)
    }
    expect(results).toHaveLength(1)
    expect(results[0]).toEqual(['{"a":1}', '{"b":2}'])
  })

  it('handles partial lines across chunks', async () => {
    const reader = createMockReader(['data: {"par', 'tial":true}\n'])
    const results: string[][] = []
    for await (const batch of readSSEStream(reader)) {
      results.push(batch)
    }
    expect(results).toHaveLength(1)
    expect(results[0]).toEqual(['{"partial":true}'])
  })

  it('skips [DONE] sentinel', async () => {
    const reader = createMockReader(['data: hello\ndata: [DONE]\n'])
    const results: string[][] = []
    for await (const batch of readSSEStream(reader)) {
      results.push(batch)
    }
    expect(results).toHaveLength(1)
    expect(results[0]).toEqual(['hello'])
  })

  it('handles empty stream', async () => {
    const reader = createMockReader([])
    const results: string[][] = []
    for await (const batch of readSSEStream(reader)) {
      results.push(batch)
    }
    expect(results).toHaveLength(0)
  })

  it('handles multiple chunks with buffering', async () => {
    const reader = createMockReader(['data: chunk1\n', 'data: chunk2\ndata: chu', 'nk3\n'])
    const all: string[] = []
    for await (const batch of readSSEStream(reader)) {
      all.push(...batch)
    }
    expect(all).toEqual(['chunk1', 'chunk2', 'chunk3'])
  })
})
