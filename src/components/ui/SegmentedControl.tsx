/**
 * SegmentedControl Component
 *
 * Pencil macOS-inspired segmented button group.
 * rounded-8, fill #111114, border #ffffff0a, active segment #0A84FF.
 */

import { type Component, For } from 'solid-js'

/** Segmented button class builder (active/inactive states) */
export function segmentedBtnClass(active: boolean): string {
  return `px-4 py-2 text-[13px] rounded-[6px] transition-colors ${
    active ? 'bg-[#0A84FF] text-white font-medium' : 'text-[#48484A] hover:text-[#C8C8CC]'
  }`
}

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
      class={`inline-flex items-center ${props.class ?? ''}`}
      style={{
        'border-radius': '8px',
        background: '#111114',
        border: '1px solid #ffffff0a',
        padding: '3px',
        gap: '2px',
      }}
    >
      <For each={props.options}>
        {(option) => {
          const isActive = (): boolean => props.value === option.id
          return (
            <button
              type="button"
              data-active={isActive() ? '' : undefined}
              onClick={() => props.onChange(option.id)}
              class="flex items-center justify-center select-none cursor-pointer"
              style={{
                'border-radius': '6px',
                height: '28px',
                padding: '0 20px',
                background: isActive() ? '#0A84FF' : 'transparent',
                color: isActive() ? '#FFFFFF' : '#48484A',
                'font-family': 'Geist, sans-serif',
                'font-size': '13px',
                'font-weight': isActive() ? '500' : '400',
                border: 'none',
                transition: 'background 150ms, color 150ms',
              }}
            >
              {option.label}
            </button>
          )
        }}
      </For>
    </div>
  )
}
