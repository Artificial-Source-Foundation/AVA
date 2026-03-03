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

  it('logs session:status events', () => {
    const { api } = createMockExtensionAPI()
    withHook(api as unknown as Record<string, unknown>)
    activate(api)
    api.emit('session:status', { sessionId: 'sess-1', status: 'busy' })
    expect(api.log.debug).toHaveBeenCalledWith('Session status: sess-1 -> busy')
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
  it('returns "truncate" for short sessions', () => {
    expect(selectStrategyName(5)).toBe('truncate')
    expect(selectStrategyName(10)).toBe('truncate')
    expect(selectStrategyName(SUMMARIZE_THRESHOLD)).toBe('truncate')
  })

  it('returns "summarize" for long sessions', () => {
    expect(selectStrategyName(SUMMARIZE_THRESHOLD + 1)).toBe('summarize')
    expect(selectStrategyName(50)).toBe('summarize')
    expect(selectStrategyName(100)).toBe('summarize')
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
