/**
 * Motion utilities for Estela
 *
 * Reusable spring presets and reduced-motion hook.
 * Uses solid-motionone (Motion One for SolidJS).
 */

import type { Accessor } from 'solid-js'
import { createSignal, onCleanup, onMount } from 'solid-js'

/** Spring animation presets for solid-motionone transition options */
export const springs = {
  /** Gentle spring - dialogs, overlays */
  gentle: { easing: 'spring(1, 120, 14, 0)' },
  /** Snappy spring - buttons, toggles */
  snappy: { easing: 'spring(1, 300, 20, 0)' },
  /** Bouncy spring - badges, playful elements */
  bouncy: { easing: 'spring(1, 200, 10, 0)' },
} as const

/** Detects prefers-reduced-motion and tracks changes reactively */
export function useReducedMotion(): Accessor<boolean> {
  const mql = window.matchMedia('(prefers-reduced-motion: reduce)')
  const [reduced, setReduced] = createSignal(mql.matches)

  const handler = (e: MediaQueryListEvent) => setReduced(e.matches)
  onMount(() => mql.addEventListener('change', handler))
  onCleanup(() => mql.removeEventListener('change', handler))

  return reduced
}
