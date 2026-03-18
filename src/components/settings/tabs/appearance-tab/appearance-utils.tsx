/**
 * Appearance Tab Shared Utilities
 *
 * Reusable primitives: SectionHeader.
 * Toggle and segmentedBtnClass are now imported from shared ui components.
 */

import type { Component } from 'solid-js'

/** Re-export shared segmentedBtnClass for convenience */
export { segmentedBtnClass as segmentedBtn } from '../../../ui/SegmentedControl'

/** Re-export shared Toggle for convenience */
export { Toggle } from '../../../ui/Toggle'

/** Small uppercase section header */
export const SectionHeader: Component<{ title: string }> = (props) => (
  <h3 class="text-[11px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2.5">
    {props.title}
  </h3>
)
