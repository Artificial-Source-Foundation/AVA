import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it, vi } from 'vitest'
import { activate } from './index.js'

vi.mock('./pipeline.js', async (importOriginal) => {
  const original = await importOriginal<typeof import('./pipeline.js')>()
  return {
    ...original,
    runPipeline: vi.fn().mockResolvedValue({
      passed: true,
      results: [],
      totalDurationMs: 10,
      summary: { total: 0, passed: 0, failed: 0, totalErrors: 0, totalWarnings: 0 },
    }),
  }
})

describe('validator extension', () => {
  it('activates and registers agent:completing handler', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('agent:completing')).toBe(true)
  })

  it('emits validation:result when agent:completing fires', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)
    api.emit('agent:completing', { agentId: 'test-agent', result: 'done' })

    // Wait for async pipeline to resolve
    await vi.waitFor(() => {
      const result = emittedEvents.find((e) => e.event === 'validation:result')
      expect(result).toBeDefined()
    })
  })

  it('cleans up on dispose', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activate(api)
    disposable.dispose()
    expect(eventHandlers.has('agent:completing')).toBe(false)
  })
})
