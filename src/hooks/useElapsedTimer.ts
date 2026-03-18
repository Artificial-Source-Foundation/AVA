/**
 * Elapsed Timer Hook
 *
 * Tracks elapsed seconds since a start timestamp, resetting when it becomes null.
 */

import { type Accessor, createEffect, createSignal, on, onCleanup } from 'solid-js'

/** Returns an accessor of elapsed whole seconds since `startedAt`, or 0 when idle. */
export function useElapsedTimer(startedAt: Accessor<number | null>): Accessor<number> {
  const [elapsedSeconds, setElapsedSeconds] = createSignal(0)

  createEffect(
    on(startedAt, (ts) => {
      if (!ts) {
        setElapsedSeconds(0)
        return
      }
      setElapsedSeconds(0)
      const interval = setInterval(() => {
        setElapsedSeconds(Math.floor((Date.now() - ts) / 1000))
      }, 1000)
      onCleanup(() => clearInterval(interval))
    })
  )

  return elapsedSeconds
}
