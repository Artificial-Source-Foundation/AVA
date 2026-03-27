import { describe, expect, it, vi } from 'vitest'
import { syncNestedScrollableBindings } from './nested-scrollables'

describe('syncNestedScrollableBindings', () => {
  it('adds listeners for new elements and removes them for stale ones', () => {
    const first = document.createElement('div')
    const second = document.createElement('div')
    const onEnter = vi.fn()
    const onLeave = vi.fn()

    const addFirst = vi.spyOn(first, 'addEventListener')
    const addSecond = vi.spyOn(second, 'addEventListener')
    const removeFirst = vi.spyOn(first, 'removeEventListener')

    const tracked = syncNestedScrollableBindings(new Set(), [first], onEnter, onLeave)
    expect(tracked.has(first)).toBe(true)
    expect(addFirst).toHaveBeenCalledTimes(2)

    syncNestedScrollableBindings(tracked, [second], onEnter, onLeave)
    expect(tracked.has(first)).toBe(false)
    expect(tracked.has(second)).toBe(true)
    expect(removeFirst).toHaveBeenCalledTimes(2)
    expect(addSecond).toHaveBeenCalledTimes(2)
  })
})
