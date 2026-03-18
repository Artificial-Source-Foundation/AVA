/**
 * SegmentedControl Component
 *
 * A segmented button group for mutually exclusive options.
 * Rounded container with accent-filled active segment.
 */

import { type Component, For } from 'solid-js'

export interface SegmentedControlOption {
  id: string
  label: string
}

export interface SegmentedControlProps {
  /** Available options */
  options: SegmentedControlOption[]
  /** Currently selected option ID */
  value: string
  /** Selection change handler */
  onChange: (id: string) => void
  /** Additional CSS classes */
  class?: string
}

export const SegmentedControl: Component<SegmentedControlProps> = (props) => {
  return (
    <div
      class={`
        inline-flex items-center
        rounded-[var(--radius-lg)]
        bg-[var(--surface-raised)]
        p-1
        gap-[2px]
        ${props.class ?? ''}
      `}
    >
      <For each={props.options}>
        {(option) => {
          const isActive = (): boolean => props.value === option.id
          return (
            <button
              type="button"
              data-active={isActive() ? '' : undefined}
              onClick={() => props.onChange(option.id)}
              class={`
                px-5 py-2.5
                text-[13px] font-medium
                rounded-[calc(var(--radius-lg)-2px)]
                transition-all duration-[var(--duration-fast)] ease-[var(--ease-out)]
                select-none cursor-pointer
                ${
                  isActive()
                    ? 'bg-[var(--accent)] text-white shadow-sm'
                    : 'text-[var(--text-secondary)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)]'
                }
              `}
            >
              {option.label}
            </button>
          )
        }}
      </For>
    </div>
  )
}
