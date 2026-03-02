import { createRoot } from 'solid-js'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { useModelStatus } from './useModelStatus'

// Mock onEvent
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
    createRoot((dispose) => {
      useModelStatus()
      expect(handlers.get('models:updated')?.size).toBe(1)
      expect(handlers.get('models:ready')?.size).toBe(1)
      dispose()
      expect(handlers.get('models:updated')?.size ?? 0).toBe(0)
      expect(handlers.get('models:ready')?.size ?? 0).toBe(0)
    })
  })
})
