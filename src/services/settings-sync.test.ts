import { describe, expect, it, vi } from 'vitest'
import { markPushing, startSettingsSync } from './settings-sync'

describe('settings-sync', () => {
  it('startSettingsSync returns a cleanup function (no-op stub)', () => {
    const unsub = startSettingsSync()
    expect(typeof unsub).toBe('function')
    // Calling cleanup should not throw
    unsub()
  })

  it('markPushing is a no-op and does not throw', () => {
    expect(() => markPushing()).not.toThrow()
  })

  it('does not dispatch events (core-v2 removed)', () => {
    const spy = vi.fn()
    window.addEventListener('ava:core-settings-changed', spy)

    const unsub = startSettingsSync()
    // No events should fire since startSettingsSync is a no-op
    expect(spy).not.toHaveBeenCalled()

    unsub()
    window.removeEventListener('ava:core-settings-changed', spy)
  })
})
