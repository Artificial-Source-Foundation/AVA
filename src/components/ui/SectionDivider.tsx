/**
 * SectionDivider Component
 *
 * A simple 1px horizontal divider line.
 */

import type { Component } from 'solid-js'

export interface SectionDividerProps {
  /** Additional CSS classes */
  class?: string
}

export const SectionDivider: Component<SectionDividerProps> = (props) => {
  return <hr class={`border-0 h-px w-full bg-[var(--gray-5)] ${props.class ?? ''}`} />
}
