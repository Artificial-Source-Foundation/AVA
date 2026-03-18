/**
 * StatusDot Component
 *
 * An 8px colored circle indicating connection/health status.
 */

import type { Component } from 'solid-js'

export type StatusDotStatus = 'connected' | 'disconnected' | 'error'

export interface StatusDotProps {
  /** Status to display */
  status: StatusDotStatus
  /** Additional CSS classes */
  class?: string
}

const statusColors: Record<StatusDotStatus, string> = {
  connected: 'bg-[var(--success)]',
  disconnected: 'bg-[var(--gray-6)]',
  error: 'bg-[var(--error)]',
}

export const StatusDot: Component<StatusDotProps> = (props) => {
  return (
    <span
      class={`
        inline-block
        w-2 h-2
        rounded-full
        flex-shrink-0
        ${statusColors[props.status]}
        ${props.class ?? ''}
      `}
      title={props.status}
    />
  )
}
