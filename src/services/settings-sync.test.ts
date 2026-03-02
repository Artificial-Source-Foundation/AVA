import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { markPushing, startSettingsSync } from './settings-sync'

// Mock core-bridge
const mockSettings = {
  on: vi.fn(),
  get: vi.fn(),
}

vi.mock('./core-bridge', () => ({
  getCoreSettings: () => mockSettings,
}))

describe('settings-sync', () => {
  let unsub: () => void
  let handler: (event: { type: string; category: string }) => void

  beforeEach(() => {
    mockSettings.on.mockImplementation((h: typeof handler) => {
      handler = h
      return () => {}
    })
    mockSettings.get.mockReturnValue({ maxTokens: 4096 })
    unsub = startSettingsSync()
  })

  afterEach(() => {
    unsub()
    vi.restoreAllMocks()
  })

  it('dispatches CustomEvent on category_changed', () => {
    const spy = vi.fn()
    window.addEventListener('ava:core-settings-changed', spy)
    handler({ type: 'category_changed', category: 'context' })
    expect(spy).toHaveBeenCalledOnce()
    const detail = (spy.mock.calls[0][0] as CustomEvent).detail
    expect(detail.category).toBe('context')
    expect(detail.value).toEqual({ maxTokens: 4096 })
    window.removeEventListener('ava:core-settings-changed', spy)
  })

  it('dispatches CustomEvent on category_registered', () => {
    const spy = vi.fn()
    window.addEventListener('ava:core-settings-changed', spy)
    handler({ type: 'category_registered', category: 'custom-ext' })
    expect(spy).toHaveBeenCalledOnce()
    const detail = (spy.mock.calls[0][0] as CustomEvent).detail
    expect(detail.type).toBe('category_registered')
    expect(detail.category).toBe('custom-ext')
    window.removeEventListener('ava:core-settings-changed', spy)
  })

  it('suppresses events during pushSettingsToCore', async () => {
    const spy = vi.fn()
    window.addEventListener('ava:core-settings-changed', spy)
    markPushing()
    handler({ type: 'category_changed', category: 'context' })
    expect(spy).not.toHaveBeenCalled()

    // After microtask, events should fire again
    await Promise.resolve()
    handler({ type: 'category_changed', category: 'context' })
    expect(spy).toHaveBeenCalledOnce()
    window.removeEventListener('ava:core-settings-changed', spy)
  })
})
