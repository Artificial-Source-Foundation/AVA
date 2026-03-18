/**
 * Permissions Tab — shared helpers and types
 */

import type { Component } from 'solid-js'

export const SectionHeader: Component<{ title: string }> = (props) => (
  <h3 class="text-[11px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2.5">
    {props.title}
  </h3>
)

export function segmentedBtn(active: boolean): string {
  return `px-4 py-2 text-[13px] rounded-[var(--radius-md)] transition-colors ${
    active
      ? 'bg-[var(--accent)] text-white'
      : 'bg-[var(--surface-raised)] text-[var(--text-secondary)] hover:bg-[var(--alpha-white-8)]'
  }`
}
