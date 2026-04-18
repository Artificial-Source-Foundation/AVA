import { describe, expect, it } from 'vitest'

import { createBoundedSessionCache } from './bounded-session-cache'

describe('createBoundedSessionCache', () => {
  it('evicts the least recently used session when capacity is exceeded', () => {
    const cache = createBoundedSessionCache<number>(2)

    cache.set('session-a', 1)
    cache.set('session-b', 2)
    expect(cache.get('session-a')).toBe(1)

    cache.set('session-c', 3)

    expect(cache.get('session-a')).toBe(1)
    expect(cache.get('session-b')).toBeUndefined()
    expect(cache.get('session-c')).toBe(3)
  })

  it('promotes a hit to most-recently used', () => {
    const cache = createBoundedSessionCache<number>(2)

    cache.set('session-a', 1)
    cache.set('session-b', 2)
    expect(cache.get('session-b')).toBe(2)

    cache.set('session-c', 3)

    expect(cache.get('session-a')).toBeUndefined()
    expect(cache.get('session-b')).toBe(2)
    expect(cache.get('session-c')).toBe(3)
  })

  it('does not promote peek-only reads', () => {
    const cache = createBoundedSessionCache<number>(2)

    cache.set('session-a', 1)
    cache.set('session-b', 2)

    expect(cache.peek('session-a')).toBe(1)

    cache.set('session-c', 3)

    expect(cache.get('session-a')).toBeUndefined()
    expect(cache.get('session-b')).toBe(2)
    expect(cache.get('session-c')).toBe(3)
  })
})
