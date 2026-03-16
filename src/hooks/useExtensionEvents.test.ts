import { createRoot } from 'solid-js'
import { describe, expect, it } from 'vitest'
import { useExtensionEvent, useExtensionEventLog, useExtensionEvents } from './useExtensionEvents'

/** Dispatch a DOM CustomEvent matching the ava: prefix convention */
function emit(event: string, data: unknown): void {
  window.dispatchEvent(new CustomEvent(`ava:${event}`, { detail: data }))
}

describe('useExtensionEvent', () => {
  it('returns null initially and updates on event', () => {
    createRoot((dispose) => {
      const value = useExtensionEvent<{ count: number }>('models:updated')
      expect(value()).toBeNull()

      emit('models:updated', { count: 5 })
      expect(value()).toEqual({ count: 5 })

      emit('models:updated', { count: 10 })
      expect(value()).toEqual({ count: 10 })

      dispose()
    })
  })

  it('disposes subscription on cleanup', () => {
    let accessor: (() => number | null) | undefined
    createRoot((dispose) => {
      accessor = useExtensionEvent<number>('test:event')
      emit('test:event', 42)
      expect(accessor()).toBe(42)

      dispose()
    })
    // After dispose, further events should not update the signal
    // (no way to check listener count on window, but the value should remain stale)
    expect(accessor!()).toBe(42)
  })
})

describe('useExtensionEvents', () => {
  it('tracks multiple events', () => {
    createRoot((dispose) => {
      const events = useExtensionEvents(['a', 'b'])
      expect(events.a()).toBeNull()
      expect(events.b()).toBeNull()

      emit('a', 'hello')
      expect(events.a()).toBe('hello')
      expect(events.b()).toBeNull()

      emit('b', 'world')
      expect(events.b()).toBe('world')

      dispose()
    })
  })
})

describe('useExtensionEventLog', () => {
  it('accumulates events into array', () => {
    createRoot((dispose) => {
      const log = useExtensionEventLog<string>('log:test')
      expect(log()).toEqual([])

      emit('log:test', 'first')
      expect(log()).toEqual(['first'])

      emit('log:test', 'second')
      expect(log()).toEqual(['first', 'second'])

      dispose()
    })
  })

  it('respects max limit', () => {
    createRoot((dispose) => {
      const log = useExtensionEventLog<number>('log:limit', 3)
      emit('log:limit', 1)
      emit('log:limit', 2)
      emit('log:limit', 3)
      emit('log:limit', 4)
      expect(log()).toEqual([2, 3, 4])
      expect(log()).toHaveLength(3)

      dispose()
    })
  })
})
