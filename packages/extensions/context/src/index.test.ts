/**
 * Context extension — activation, strategy registration, event handling.
 */

import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate, SUMMARIZE_THRESHOLD, selectStrategyName } from './index.js'

describe('context extension activation', () => {
  it('registers both compaction strategies', () => {
    const { api, registeredContextStrategies } = createMockExtensionAPI()
    activate(api)
    const names = registeredContextStrategies.map((s) => s.name)
    expect(names).toContain('truncate')
    expect(names).toContain('summarize')
  })

  it('listens for llm:usage events', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('llm:usage')).toBe(true)
  })

  it('listens for context:compacted events', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('context:compacted')).toBe(true)
  })

  it('cleans up on dispose', () => {
    const { api, eventHandlers, registeredContextStrategies } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(registeredContextStrategies.length).toBe(2)
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
})
