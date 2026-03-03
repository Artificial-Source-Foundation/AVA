import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { installMockPlatform } from '../../../core-v2/src/__test-utils__/mock-platform.js'
import { registerProvider, resetProviders } from '../../../core-v2/src/llm/client.js'
import type { StreamDelta } from '../../../core-v2/src/llm/types.js'
import { inlineSuggestTool } from './inline-suggest.js'
import { clearInlineSuggestCache } from './inline-suggest-cache.js'

const ctx = {
  sessionId: 'session',
  workingDirectory: '/tmp',
  signal: new AbortController().signal,
}

describe('inlineSuggestTool', () => {
  beforeEach(() => {
    installMockPlatform()
    clearInlineSuggestCache()
  })

  afterEach(() => {
    resetProviders()
    clearInlineSuggestCache()
  })

  it('builds FIM completion and returns suggestion', async () => {
    const platform = installMockPlatform()
    platform.fs.addFile('/tmp/example.ts', 'function sum(a, b) {\n  return \n}\n')

    registerProvider('anthropic', () => ({
      async *stream(messages): AsyncGenerator<StreamDelta, void, unknown> {
        const prompt = String(messages[0]?.content)
        expect(prompt).toContain('<fim_prefix>')
        expect(prompt).toContain('<fim_suffix>')
        expect(prompt).toContain('<fim_middle>')
        yield { content: 'a + b' }
        yield { done: true }
      },
    }))

    const result = await inlineSuggestTool.execute(
      {
        path: '/tmp/example.ts',
        line: 2,
        column: 10,
      },
      ctx
    )

    expect(result.success).toBe(true)
    expect(result.output).toBe('a + b')
    expect(result.metadata?.cached).toBe(false)
  })

  it('uses cache to avoid repeated provider calls', async () => {
    const platform = installMockPlatform()
    platform.fs.addFile('/tmp/example.ts', 'const answer = \n')
    let calls = 0

    registerProvider('anthropic', () => ({
      async *stream(): AsyncGenerator<StreamDelta, void, unknown> {
        calls += 1
        yield { content: '42' }
        yield { done: true }
      },
    }))

    const input = { path: '/tmp/example.ts', line: 1, column: 16 }
    const first = await inlineSuggestTool.execute(input, ctx)
    const second = await inlineSuggestTool.execute(input, ctx)

    expect(first.success).toBe(true)
    expect(second.success).toBe(true)
    expect(second.metadata?.cached).toBe(true)
    expect(calls).toBe(1)
  })
})
