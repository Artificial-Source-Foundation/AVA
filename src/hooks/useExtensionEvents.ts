/**
 * Extension Event Bridge
 * Reactive SolidJS hooks that bridge core-v2 extension events → signals.
 */

import { onEvent } from '@ava/core-v2/extensions'
import { type Accessor, createSignal, onCleanup } from 'solid-js'

/**
 * Subscribe to a single extension event and expose its latest value as a signal.
 *
 * @param eventName - The event to listen to (e.g. 'models:updated', 'context:compacted')
 * @returns Accessor for the latest event data, or null if not yet received
 */
export function useExtensionEvent<T = unknown>(eventName: string): Accessor<T | null> {
  const [value, setValue] = createSignal<T | null>(null)
  const disposable = onEvent(eventName, (data) => {
    setValue(() => data as T)
  })
  onCleanup(() => disposable.dispose())
  return value
}

/**
 * Subscribe to multiple extension events and expose them as a record of signals.
 *
 * @param eventNames - Array of event names to listen to
 * @returns Record mapping event names to their latest value accessors
 */
export function useExtensionEvents(eventNames: string[]): Record<string, Accessor<unknown>> {
  const result: Record<string, Accessor<unknown>> = {}
  const disposables: Array<{ dispose: () => void }> = []

  for (const name of eventNames) {
    const [value, setValue] = createSignal<unknown>(null)
    const disposable = onEvent(name, (data) => {
      setValue(() => data)
    })
    result[name] = value
    disposables.push(disposable)
  }

  onCleanup(() => {
    for (const d of disposables) d.dispose()
  })

  return result
}

/**
 * Subscribe to a single event and accumulate values into a log array.
 *
 * @param eventName - The event to listen to
 * @param max - Maximum entries to keep (default 100)
 * @returns Accessor for the event log array
 */
export function useExtensionEventLog<T = unknown>(eventName: string, max = 100): Accessor<T[]> {
  const [log, setLog] = createSignal<T[]>([])
  const disposable = onEvent(eventName, (data) => {
    setLog((prev) => {
      const next = [...prev, data as T]
      return next.length > max ? next.slice(-max) : next
    })
  })
  onCleanup(() => disposable.dispose())
  return log
}
