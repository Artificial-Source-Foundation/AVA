/**
 * BackLink Component
 *
 * Navigation back-link with arrow-left icon.
 * Used at the top of Settings sidebar.
 */

import { ArrowLeft } from 'lucide-solid'
import type { Component } from 'solid-js'

export interface BackLinkProps {
  /** Link label (default: "Back to Chat") */
  label?: string
  /** Click handler */
  onClick: () => void
  /** Additional CSS classes */
  class?: string
}

export const BackLink: Component<BackLinkProps> = (props) => {
  return (
    <button
      type="button"
      onClick={() => props.onClick()}
      class={`
        inline-flex items-center gap-1.5
        text-[12px] text-[var(--gray-7)]
        hover:text-[var(--text-secondary)]
        transition-colors duration-[var(--duration-fast)] ease-[var(--ease-out)]
        select-none cursor-pointer
        bg-transparent border-none p-0
        focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:rounded-sm
        ${props.class ?? ''}
      `}
    >
      <ArrowLeft size={14} />
      <span>{props.label ?? 'Back to Chat'}</span>
    </button>
  )
}
