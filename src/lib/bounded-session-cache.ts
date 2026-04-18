export interface BoundedSessionCache<T> {
  get: (sessionId: string) => T | undefined
  peek: (sessionId: string) => T | undefined
  set: (sessionId: string, value: T) => void
  delete: (sessionId: string) => void
  clear: () => void
  has: (sessionId: string) => boolean
  size: () => number
}

export function createBoundedSessionCache<T>(maxEntries: number): BoundedSessionCache<T> {
  const limit = Math.max(1, Math.floor(maxEntries))
  const cache = new Map<string, T>()

  const touch = (sessionId: string, value: T): void => {
    cache.delete(sessionId)
    cache.set(sessionId, value)
    while (cache.size > limit) {
      const oldestKey = cache.keys().next().value
      if (!oldestKey) {
        break
      }
      cache.delete(oldestKey)
    }
  }

  return {
    get: (sessionId) => {
      const value = cache.get(sessionId)
      if (value === undefined) {
        return undefined
      }
      touch(sessionId, value)
      return value
    },
    peek: (sessionId) => cache.get(sessionId),
    set: (sessionId, value) => {
      if (!sessionId) {
        return
      }
      touch(sessionId, value)
    },
    delete: (sessionId) => {
      cache.delete(sessionId)
    },
    clear: () => {
      cache.clear()
    },
    has: (sessionId) => cache.has(sessionId),
    size: () => cache.size,
  }
}
