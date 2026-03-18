/**
 * GroupHeader Component
 *
 * Uppercase section label used in settings sidebar navigation.
 * "DESKTOP", "AI", "EXTENSIONS", "ADVANCED", etc.
 */

import type { Component } from 'solid-js'

export interface GroupHeaderProps {
  /** Label text (rendered uppercase) */
  label: string
  /** Additional CSS classes */
  class?: string
}

export const GroupHeader: Component<GroupHeaderProps> = (props) => {
  return (
    <span
      class={`
        block
        text-[10px] font-medium uppercase
        tracking-[0.8px]
        text-[var(--gray-6)]
        select-none
        ${props.class ?? ''}
      `}
    >
      {props.label}
    </span>
  )
}
