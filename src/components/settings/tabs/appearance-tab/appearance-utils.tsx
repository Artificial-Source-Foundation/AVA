/**
 * Appearance Tab Shared Utilities
 *
 * Reusable primitives: segmented button helper, SectionHeader, Toggle.
 */

import type { Component } from 'solid-js'

/** Segmented button class builder (active/inactive states) */
export function segmentedBtn(active: boolean): string {
  return `px-4 py-2 text-[13px] rounded-[var(--radius-md)] transition-colors ${
    active
      ? 'bg-[var(--accent)] text-white'
      : 'bg-[var(--surface-raised)] text-[var(--text-secondary)] hover:bg-[var(--alpha-white-8)]'
  }`
}

/** Small uppercase section header */
export const SectionHeader: Component<{ title: string }> = (props) => (
  <h3 class="text-[11px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2.5">
    {props.title}
  </h3>
)

/** Minimal toggle switch */
export const Toggle: Component<{ checked: boolean; onChange: (v: boolean) => void }> = (props) => (
  <button
    type="button"
    onClick={() => props.onChange(!props.checked)}
    class={`
      relative w-11 h-6 rounded-full transition-colors
      ${props.checked ? 'bg-[var(--accent)]' : 'bg-[var(--border-strong)]'}
    `}
  >
    <span
      class="absolute top-[2px] left-[2px] w-5 h-5 rounded-full bg-white transition-transform"
      style={{
        transform: props.checked ? 'translateX(20px)' : 'translateX(0)',
      }}
    />
  </button>
)
