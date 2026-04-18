import { type Accessor, createSignal } from 'solid-js'

export interface EventHistoryDelta<T> {
  cursor: number
  events: T[]
}

export interface EventHistory<T> {
  events: Accessor<T[]>
  version: Accessor<number>
  cursor: () => number
  append: (event: T) => void
  clear: () => void
  replace: (events: T[]) => void
  snapshot: () => T[]
  readSince: (cursor: number) => EventHistoryDelta<T>
}

export function createBoundedEventHistory<T>(maxSize: number): EventHistory<T> {
  const capacity = Math.max(1, Math.floor(maxSize))
  let buffer = new Array<T>(capacity)
  let head = 0
  let size = 0
  let startCursor = 0
  let endCursor = 0
  let currentVersion = 0
  let cachedSnapshot: T[] = []
  let cachedSnapshotVersion = -1
  const [version, setVersion] = createSignal(0)

  const invalidateSnapshot = (): void => {
    cachedSnapshotVersion = -1
  }

  const notify = (): void => {
    currentVersion += 1
    setVersion(currentVersion)
  }

  const materialize = (): T[] => {
    if (cachedSnapshotVersion === currentVersion) {
      return cachedSnapshot
    }

    const snapshot = new Array<T>(size)
    for (let i = 0; i < size; i += 1) {
      snapshot[i] = buffer[(head + i) % capacity]!
    }

    cachedSnapshot = snapshot
    cachedSnapshotVersion = currentVersion
    return snapshot
  }

  return {
    events: () => {
      version()
      return materialize()
    },
    version,
    cursor: () => endCursor,
    append: (event) => {
      if (size < capacity) {
        buffer[(head + size) % capacity] = event
        size += 1
      } else {
        buffer[head] = event
        head = (head + 1) % capacity
        startCursor += 1
      }
      endCursor += 1
      invalidateSnapshot()
      notify()
    },
    clear: () => {
      if (size === 0) return

      buffer = new Array<T>(capacity)
      head = 0
      size = 0
      startCursor = endCursor
      invalidateSnapshot()
      notify()
    },
    replace: (events) => {
      const next =
        events.length > capacity ? events.slice(events.length - capacity) : events.slice()

      buffer = new Array<T>(capacity)
      head = 0
      size = next.length
      startCursor = 0
      endCursor = next.length
      for (let i = 0; i < next.length; i += 1) {
        buffer[i] = next[i]!
      }

      invalidateSnapshot()
      notify()
    },
    snapshot: () => materialize().slice(),
    readSince: (cursor) => {
      const effectiveCursor = Math.max(startCursor, Math.min(cursor, endCursor))
      const retainedCount = endCursor - effectiveCursor

      if (retainedCount <= 0) {
        return { cursor: endCursor, events: [] }
      }

      const firstLogicalIndex = effectiveCursor - startCursor
      const eventsSinceCursor = new Array<T>(retainedCount)
      for (let i = 0; i < retainedCount; i += 1) {
        eventsSinceCursor[i] = buffer[(head + firstLogicalIndex + i) % capacity]!
      }

      return {
        cursor: endCursor,
        events: eventsSinceCursor,
      }
    },
  }
}
