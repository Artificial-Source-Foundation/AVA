import { createRoot } from 'solid-js'
import { describe, expect, it } from 'vitest'
import { useModelStatus } from './useModelStatus'

/** Dispatch a DOM CustomEvent matching the ava: prefix convention */
function emit(event: string, data: unknown): void {
  window.dispatchEvent(new CustomEvent(`ava:${event}`, { detail: data }))
}

describe('useModelStatus', () => {
  it('starts with zero count', () => {
    createRoot((dispose) => {
      const { modelCount, lastUpdate } = useModelStatus()
      expect(modelCount()).toBe(0)
      expect(lastUpdate()).toBeNull()
      dispose()
    })
  })

  it('updates on models:updated event', () => {
    createRoot((dispose) => {
      const { modelCount, lastUpdate } = useModelStatus()
      emit('models:updated', { count: 14 })
      expect(modelCount()).toBe(14)
      expect(lastUpdate()).toBeTypeOf('number')
      dispose()
    })
  })

  it('updates on models:ready event', () => {
    createRoot((dispose) => {
      const { modelCount } = useModelStatus()
      emit('models:ready', { count: 8 })
      expect(modelCount()).toBe(8)
      dispose()
    })
  })

  it('refresh updates lastUpdate timestamp', () => {
    createRoot((dispose) => {
      const { lastUpdate, refresh } = useModelStatus()
      expect(lastUpdate()).toBeNull()
      refresh()
      expect(lastUpdate()).toBeTypeOf('number')
      dispose()
    })
  })

  it('cleans up subscriptions', () => {
    // After dispose, events should no longer update the signals
    let modelCountAccessor: (() => number) | undefined
    createRoot((dispose) => {
      const { modelCount } = useModelStatus()
      modelCountAccessor = modelCount
      emit('models:updated', { count: 5 })
      expect(modelCount()).toBe(5)
      dispose()
    })
    // After dispose, value is stale (no new updates)
    emit('models:updated', { count: 99 })
    expect(modelCountAccessor!()).toBe(5)
  })
})
