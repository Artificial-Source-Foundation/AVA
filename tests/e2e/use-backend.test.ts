import { createRoot } from 'solid-js'
import { describe, expect, it, vi } from 'vitest'

vi.mock('../../src/hooks/use-rust-agent', () => ({
  useRustAgent: () => ({
    isRunning: () => false,
    error: () => null,
    run: async () => 'rust',
    stop: () => undefined,
    cancel: async () => undefined,
    clearError: () => undefined,
    streamingContent: () => '',
    thinkingContent: () => '',
    activeToolCalls: () => [],
    lastResult: () => null,
    tokenUsage: () => ({ input: 0, output: 0, cost: 0 }),
    events: () => [],
    isStreaming: () => false,
    currentTokens: () => '',
    session: () => null,
  }),
}))

import { useBackend } from '../../src/hooks/use-backend'

describe('useBackend', () => {
  it('always uses Rust backend', () => {
    let dispose = () => {}
    let hook: ReturnType<typeof useBackend>
    createRoot((disposeRoot) => {
      dispose = disposeRoot
      hook = useBackend()
    })

    expect(hook!.backendType()).toBe('rust')
    expect(hook!.isRunning()).toBe(false)

    dispose()
  })
})
