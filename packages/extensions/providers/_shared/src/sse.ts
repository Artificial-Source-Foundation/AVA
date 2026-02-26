/**
 * SSE Stream Parsing Utilities
 * Shared SSE (Server-Sent Events) parsing for LLM provider streaming responses.
 */

export interface SSELine {
  data: string
  done: boolean
}

/**
 * Parse raw SSE lines from a text chunk.
 * Handles 'data: ' prefix, '[DONE]' sentinel, and skips event:/comment lines.
 */
export function* parseSSELines(lines: string[]): Generator<SSELine> {
  for (const line of lines) {
    if (!line.startsWith('data: ')) continue
    const data = line.slice(6).trim()
    if (!data) continue
    if (data === '[DONE]') {
      yield { data: '', done: true }
      continue
    }
    yield { data, done: false }
  }
}

/**
 * Read an SSE stream from a ReadableStreamDefaultReader.
 * Handles buffering of partial lines across chunks.
 */
export async function* readSSEStream(
  reader: ReadableStreamDefaultReader<Uint8Array>
): AsyncGenerator<string[], void, unknown> {
  const decoder = new TextDecoder()
  let buffer = ''

  try {
    while (true) {
      const { done, value } = await reader.read()
      if (done) break

      buffer += decoder.decode(value, { stream: true })
      const lines = buffer.split('\n')
      buffer = lines.pop() ?? ''

      const dataLines: string[] = []
      for (const sse of parseSSELines(lines)) {
        if (sse.done) continue
        dataLines.push(sse.data)
      }

      if (dataLines.length > 0) {
        yield dataLines
      }
    }
  } finally {
    reader.releaseLock()
  }
}
