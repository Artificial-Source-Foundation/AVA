import { type Accessor, createSignal } from 'solid-js'

export interface EventHistory<T> {
  events: Accessor<T[]>
  append: (event: T) => void
  clear: () => void
}

export function createBoundedEventHistory<T>(maxSize: number): EventHistory<T> {
  const buffer: T[] = []
  const [version, setVersion] = createSignal(0)

  const notify = (): void => {
    setVersion((current) => current + 1)
  }

  return {
    events: () => {
      version()
      return buffer
    },
    append: (event) => {
      if (buffer.length >= maxSize) {
        buffer.splice(0, buffer.length - maxSize + 1)
      }
      buffer.push(event)
      notify()
    },
    clear: () => {
      if (buffer.length === 0) return
      buffer.length = 0
      notify()
    },
  }
}
