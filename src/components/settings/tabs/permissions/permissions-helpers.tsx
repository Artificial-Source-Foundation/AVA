/**
 * Permissions Tab — shared helpers and types
 */

import type { Component } from 'solid-js'

export { segmentedBtnClass as segmentedBtn } from '../../../ui/SegmentedControl'

export const SectionHeader: Component<{ title: string }> = (props) => (
  <h3 class="text-[11px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2.5">
    {props.title}
  </h3>
)
