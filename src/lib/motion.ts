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

/**
 * Detects reduced motion — respects both OS preference and app setting.
 * Returns true if either the OS `prefers-reduced-motion: reduce` is active
 * or the user toggled "Reduce motion" in Appearance settings.
 */
export function useReducedMotion(): Accessor<boolean> {
  const mql = window.matchMedia('(prefers-reduced-motion: reduce)')
  const appReduced = () => document.documentElement.hasAttribute('data-reduce-motion')
  const [reduced, setReduced] = createSignal(mql.matches || appReduced())

  const handler = () => setReduced(mql.matches || appReduced())
  onMount(() => {
    mql.addEventListener('change', handler)
    // Watch for data-reduce-motion attribute changes
    const observer = new MutationObserver(handler)
    observer.observe(document.documentElement, {
      attributes: true,
      attributeFilter: ['data-reduce-motion'],
    })
    onCleanup(() => {
      mql.removeEventListener('change', handler)
      observer.disconnect()
    })
  })

  return reduced
}
