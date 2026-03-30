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

/** Section header matching the standard settings pattern: Geist 14px/500 #F5F5F7 */
export const SectionHeader: Component<{ title: string }> = (props) => (
  <span class="settings-section-title">{props.title}</span>
)
