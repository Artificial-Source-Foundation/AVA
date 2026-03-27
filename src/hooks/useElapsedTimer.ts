/**
 * Elapsed Timer Hook
 *
 * Tracks elapsed seconds since a start timestamp, resetting when it becomes null.
 * All consumers share a single 1 Hz ticker so large tool lists do not spin up
 * one interval per card.
 */

import { type Accessor, createEffect, createSignal, onCleanup } from 'solid-js'

const [tickNow, setTickNow] = createSignal(Date.now())
let tickerRefCount = 0
let tickerHandle: number | undefined

function retainTicker(): void {
  if (typeof window === 'undefined') return
  tickerRefCount += 1
  if (tickerRefCount === 1) {
    tickerHandle = window.setInterval(() => {
      setTickNow(Date.now())
    }, 1000)
  }
}

function releaseTicker(): void {
  if (tickerRefCount === 0) return
  tickerRefCount -= 1
  if (tickerRefCount === 0 && tickerHandle !== undefined) {
    window.clearInterval(tickerHandle)
    tickerHandle = undefined
  }
}

/** Returns a shared 1 Hz timestamp ticker for lightweight elapsed-label formatting. */
export function useSecondTicker(active: Accessor<boolean>): Accessor<number> {
  createEffect(() => {
    if (!active()) return
    retainTicker()
    tickNow()
    onCleanup(releaseTicker)
  })

  return tickNow
}

/** Returns an accessor of elapsed whole seconds since `startedAt`, or 0 when idle. */
export function useElapsedTimer(startedAt: Accessor<number | null>): Accessor<number> {
  const [elapsedSeconds, setElapsedSeconds] = createSignal(0)

  createEffect(() => {
    const ts = startedAt()
    if (!ts) {
      setElapsedSeconds(0)
      return
    }

    retainTicker()
    tickNow()
    setElapsedSeconds(Math.floor((Date.now() - ts) / 1000))
    onCleanup(releaseTicker)
  })

  return elapsedSeconds
}
