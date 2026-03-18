/**
 * StepDots
 *
 * Reusable step indicator dots for the onboarding flow.
 * Active dot uses accent color (scaled up), completed dots are solid accent, rest use gray-5.
 */

import { type Component, For } from 'solid-js'

export interface StepDotsProps {
  total: number
  current: number
}

export const StepDots: Component<StepDotsProps> = (props) => (
  <div class="flex items-center justify-center gap-2">
    <For each={Array.from({ length: props.total })}>
      {(_, i) => (
        <div
          class="w-2 h-2 rounded-full transition-all duration-300"
          classList={{
            'bg-[var(--accent)] scale-125': i() === props.current,
            'bg-[var(--accent)]': i() < props.current,
            'bg-[var(--gray-5)]': i() > props.current,
          }}
        />
      )}
    </For>
  </div>
)
