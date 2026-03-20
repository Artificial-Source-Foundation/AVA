/**
 * CompactionDivider
 *
 * A thin horizontal rule shown in the message list when context has been
 * compacted.  Messages that appear above the divider are rendered at reduced
 * opacity to signal they were part of the compacted history.
 */

import type { Component } from 'solid-js'

export const CompactionDivider: Component = () => (
  <div class="compaction-divider">
    <div class="compaction-divider__line" />
    <span class="compaction-divider__label">Previous context compacted</span>
    <div class="compaction-divider__line" />
  </div>
)
