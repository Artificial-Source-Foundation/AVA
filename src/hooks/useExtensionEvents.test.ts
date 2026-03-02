import { createRoot } from 'solid-js'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { useExtensionEvent, useExtensionEventLog, useExtensionEvents } from './useExtensionEvents'

// Mock onEvent — capture registered handlers
const handlers = new Map<string, Set<(data: unknown) => void>>()

vi.mock('@ava/core-v2/extensions', () => ({
  onEvent: (event: string, handler: (data: unknown) => void) => {
    if (!handlers.has(event)) handlers.set(event, new Set())
    handlers.get(event)!.add(handler)
    return { dispose: () => handlers.get(event)?.delete(handler) }
  },
}))

function emit(event: string, data: unknown): void {
  const set = handlers.get(event)
  if (set) for (const h of set) h(data)
}

afterEach(() => {
  handlers.clear()
})

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
    createRoot((dispose) => {
      const value = useExtensionEvent<number>('test:event')
      emit('test:event', 42)
      expect(value()).toBe(42)

      dispose()
      // After dispose, handler set should be empty
      expect(handlers.get('test:event')?.size ?? 0).toBe(0)
    })
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
