import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { activate } from './index.js'

describe('scheduler extension', () => {
  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('activates and starts interval', () => {
    vi.useFakeTimers()
    const { api } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(api.log.debug).toHaveBeenCalledWith('Scheduler extension activated')
    disposable.dispose()
    vi.useRealTimers()
  })

  it('listens for scheduler:register and scheduler:unregister', () => {
    vi.useFakeTimers()
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(eventHandlers.has('scheduler:register')).toBe(true)
    expect(eventHandlers.has('scheduler:unregister')).toBe(true)
    disposable.dispose()
    vi.useRealTimers()
  })

  it('registers tasks via event', () => {
    vi.useFakeTimers()
    const { api } = createMockExtensionAPI()
    const disposable = activate(api)

    api.emit('scheduler:register', {
      id: 'test',
      name: 'test-task',
      interval: 1000,
      nextRun: 0,
      handler: async () => {},
    })

    expect(api.log.debug).toHaveBeenCalledWith('Scheduled task registered: test-task')
    disposable.dispose()
    vi.useRealTimers()
  })

  it('cleans up interval and listeners on dispose', () => {
    vi.useFakeTimers()
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activate(api)
    disposable.dispose()
    expect(eventHandlers.has('scheduler:register')).toBe(false)
    vi.useRealTimers()
  })
})
