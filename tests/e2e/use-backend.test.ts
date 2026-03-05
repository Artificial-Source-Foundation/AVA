import { createRoot } from 'solid-js'
import { describe, expect, it, vi } from 'vitest'

vi.mock('../../src/hooks/useAgent', () => ({
  useAgent: () => ({
    isRunning: () => false,
    isStreaming: () => false,
    error: () => null,
    run: async () => 'typescript',
    cancel: () => undefined,
    stopAgent: () => undefined,
    clearError: () => undefined,
  }),
}))

vi.mock('../../src/hooks/use-rust-agent', () => ({
  useRustAgent: () => ({
    isRunning: () => false,
    error: () => null,
    run: async () => 'rust',
    stop: () => undefined,
    clearError: () => undefined,
  }),
}))

import { useBackend } from '../../src/hooks/use-backend'
import { useSettings } from '../../src/stores/settings'

describe('useBackend', () => {
  it('switches between TS and Rust backend based on setting', async () => {
    const settings = useSettings()
    settings.updateAgentBackend('core-v2')

    let dispose = () => {}
    let hook: ReturnType<typeof useBackend>
    createRoot((disposeRoot) => {
      dispose = disposeRoot
      hook = useBackend()
    })

    expect(await hook!.run('goal')).toBe('typescript')

    settings.updateAgentBackend('core')
    expect(await hook!.run('goal')).toBe('rust')

    dispose()
  })
})
