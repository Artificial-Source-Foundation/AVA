/**
 * Chip Toggle — compact, toggleable label
 *
 * Used in agents-tab-detail for tool and capability toggles.
 */

import type { Component } from 'solid-js'

export const Chip: Component<{ label: string; active: boolean; onClick: () => void }> = (props) => (
  <button
    type="button"
    onClick={props.onClick}
    class={`px-1.5 py-0.5 text-[10px] rounded-[var(--radius-sm)] transition-colors ${
      props.active
        ? 'bg-[var(--accent)] text-white'
        : 'bg-[var(--alpha-white-5)] text-[var(--text-tertiary)] hover:bg-[var(--alpha-white-8)] hover:text-[var(--text-secondary)]'
    }`}
  >
    {props.label}
  </button>
)
