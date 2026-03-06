/**
 * Context extension — activation, strategy registration, event handling.
 */

import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate, SUMMARIZE_THRESHOLD, selectStrategyName } from './index.js'

function withHook(api: Record<string, unknown>): void {
  api.registerHook = () => ({ dispose() {} })
}

describe('context extension activation', () => {
  it('registers expanded compaction strategies', () => {
    const { api, registeredContextStrategies } = createMockExtensionAPI()
    withHook(api as unknown as Record<string, unknown>)
    activate(api)
    const names = registeredContextStrategies.map((s) => s.name)
    expect(names).toContain('prune')
    expect(names).toContain('truncate')
    expect(names).toContain('summarize')
    expect(names).toContain('backward-fifo')
    expect(names).toContain('sliding-window')
    expect(names).toContain('observation-masking')
    expect(names).toContain('amortized-forgetting')
    expect(names).toContain('tiered-compaction')
  })

  it('listens for llm:usage events', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    withHook(api as unknown as Record<string, unknown>)
    activate(api)
    expect(eventHandlers.has('llm:usage')).toBe(true)
  })

  it('listens for context:compacted events', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    withHook(api as unknown as Record<string, unknown>)
    activate(api)
    expect(eventHandlers.has('context:compacted')).toBe(true)
  })

  it('listens for session:status events', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    withHook(api as unknown as Record<string, unknown>)
    activate(api)
    expect(eventHandlers.has('session:status')).toBe(true)
  })

  it('listens for prompt:build events for per-turn context injection', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    withHook(api as unknown as Record<string, unknown>)
    activate(api)
    expect(eventHandlers.has('prompt:build')).toBe(true)
  })

  it('logs session:status events', () => {
    const { api } = createMockExtensionAPI()
    withHook(api as unknown as Record<string, unknown>)
    activate(api)
    api.emit('session:status', { sessionId: 'sess-1', status: 'busy' })
    expect(api.log.debug).toHaveBeenCalledWith('Session status: sess-1 -> busy')
  })

  it('injects one-line turn context into prompt sections', () => {
    const { api } = createMockExtensionAPI()
    withHook(api as unknown as Record<string, unknown>)
    activate(api)

    api.emit('session:opened', { sessionId: 's1', workingDirectory: '/project' })
    api.emit('extensions:loaded', { count: 31 })
    api.emit('turn:start', { turn: 3 })
    api.emit('llm:usage', {
      sessionId: 's1',
      inputTokens: 45_000,
      outputTokens: 45_000,
    })

    const sections: string[] = []
    api.emit('prompt:build', { sections })

    expect(sections).toHaveLength(1)
    expect(sections[0]).toContain('[Context] CWD: /project')
    expect(sections[0]).toContain('Tokens: 45% of 200K')
    expect(sections[0]).toContain('Turn: 3/50')
    expect(sections[0]).toContain('Extensions: 31 active')
  })

  it('cleans up on dispose', () => {
    const { api, eventHandlers, registeredContextStrategies } = createMockExtensionAPI()
    withHook(api as unknown as Record<string, unknown>)
    const disposable = activate(api)
    expect(registeredContextStrategies.length).toBeGreaterThanOrEqual(7)
    expect(eventHandlers.size).toBeGreaterThan(0)

    disposable!.dispose()
    // After dispose, strategies should be unregistered
    expect(registeredContextStrategies.length).toBe(0)
  })
})

describe('selectStrategyName', () => {
  it('returns tiered-compaction for auto strategy', () => {
    expect(selectStrategyName(5)).toBe('tiered-compaction')
    expect(selectStrategyName(10)).toBe('tiered-compaction')
    expect(selectStrategyName(SUMMARIZE_THRESHOLD + 1)).toBe('tiered-compaction')
  })

  it('uses threshold of 20 messages', () => {
    expect(SUMMARIZE_THRESHOLD).toBe(20)
  })

  it('returns explicit strategy from settings', () => {
    expect(selectStrategyName(100, { strategy: 'backward-fifo', historyProcessors: [] })).toBe(
      'backward-fifo'
    )
  })

  it('returns pipeline sequence from settings', () => {
    expect(
      selectStrategyName(5, {
        strategy: ['observation-masking', 'summarize'],
        historyProcessors: [],
      })
    ).toEqual(['observation-masking', 'summarize'])
  })
})
