/**
 * StepDots
 *
 * Reusable step indicator dots for the onboarding flow.
 * Active dot: 8px accent blue. Completed: 6px accent blue. Remaining: 6px #48484A.
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
          class="rounded-full transition-all duration-300"
          classList={{
            'w-2 h-2 bg-[var(--accent)]': i() === props.current,
            'w-1.5 h-1.5 bg-[var(--accent)]': i() < props.current,
            'w-1.5 h-1.5': i() > props.current,
          }}
          style={i() > props.current ? { background: '#48484A' } : undefined}
        />
      )}
    </For>
  </div>
)
