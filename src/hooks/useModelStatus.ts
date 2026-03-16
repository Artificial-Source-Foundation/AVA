/**
 * Model Status Hook
 * Reactive hook that tracks model availability from the models extension.
 */

import { type Accessor, createSignal, onCleanup } from 'solid-js'

/** Minimal event subscription via DOM CustomEvents (replaces @ava/core-v2/extensions onEvent) */
function onEvent(eventName: string, handler: (data: unknown) => void): { dispose: () => void } {
  const listener = (e: Event) => {
    handler((e as CustomEvent).detail)
  }
  if (typeof window !== 'undefined') {
    window.addEventListener(`ava:${eventName}`, listener)
  }
  return {
    dispose() {
      if (typeof window !== 'undefined') {
        window.removeEventListener(`ava:${eventName}`, listener)
      }
    },
  }
}

export interface ModelStatusInfo {
  modelCount: Accessor<number>
  lastUpdate: Accessor<number | null>
  refresh: () => void
}

/**
 * Subscribe to model registry events and expose reactive model count.
 *
 * Listens for:
 * - `models:updated` — model list changed (availability, status)
 * - `models:ready` — initial model scan complete
 */
export function useModelStatus(): ModelStatusInfo {
  const [modelCount, setModelCount] = createSignal(0)
  const [lastUpdate, setLastUpdate] = createSignal<number | null>(null)

  const disposables = [
    onEvent('models:updated', (data) => {
      const d = data as { count?: number }
      if (typeof d.count === 'number') {
        setModelCount(d.count)
      }
      setLastUpdate(Date.now())
    }),
    onEvent('models:ready', (data) => {
      const d = data as { count?: number }
      if (typeof d.count === 'number') {
        setModelCount(d.count)
      }
      setLastUpdate(Date.now())
    }),
  ]

  onCleanup(() => {
    for (const d of disposables) d.dispose()
  })

  const refresh = () => {
    setLastUpdate(Date.now())
  }

  return { modelCount, lastUpdate, refresh }
}
