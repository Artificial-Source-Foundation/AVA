import { describe, expect, it, vi } from 'vitest'
import { bindSessionWindowListeners, installSessionWindowListeners } from './window-listeners'

describe('session window listeners', () => {
  it('binds and cleans up listeners deterministically', () => {
    const calls = {
      compacted: 0,
      busy: 0,
      budget: 0,
      context: 0,
    }

    const cleanup = bindSessionWindowListeners(window, {
      onCompacted: () => calls.compacted++,
      onSessionStatus: ({ status }) => {
        if (status === 'busy') calls.busy++
      },
      onBudgetUpdated: () => calls.budget++,
      onCoreSettingsChanged: (detail) => {
        if (detail?.category === 'context') calls.context++
      },
    })

    window.dispatchEvent(new CustomEvent('ava:compacted'))
    window.dispatchEvent(
      new CustomEvent('ava:session-status', { detail: { sessionId: 's1', status: 'busy' } })
    )
    window.dispatchEvent(new CustomEvent('ava:budget-updated'))
    window.dispatchEvent(
      new CustomEvent('ava:core-settings-changed', { detail: { category: 'context' } })
    )

    expect(calls).toEqual({ compacted: 1, busy: 1, budget: 1, context: 1 })

    cleanup()
    window.dispatchEvent(new CustomEvent('ava:budget-updated'))
    expect(calls.budget).toBe(1)
  })

  it('replaces prior listeners when reinstalled', () => {
    const first = vi.fn()
    const second = vi.fn()

    installSessionWindowListeners({
      onCompacted: first,
      onSessionStatus: vi.fn(),
      onBudgetUpdated: vi.fn(),
      onCoreSettingsChanged: vi.fn(),
    })
    installSessionWindowListeners({
      onCompacted: second,
      onSessionStatus: vi.fn(),
      onBudgetUpdated: vi.fn(),
      onCoreSettingsChanged: vi.fn(),
    })

    window.dispatchEvent(new CustomEvent('ava:compacted'))

    expect(first).not.toHaveBeenCalled()
    expect(second).toHaveBeenCalledOnce()
  })
})
