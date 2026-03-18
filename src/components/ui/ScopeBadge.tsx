/**
 * ScopeBadge Component
 *
 * Small badge indicating global vs local scope.
 * Used in MCP servers, Plugins, Skills, and Commands.
 */

import type { Component } from 'solid-js'

export type ScopeBadgeScope = 'global' | 'local'

export interface ScopeBadgeProps {
  /** Scope type */
  scope: ScopeBadgeScope
  /** Additional CSS classes */
  class?: string
}

export const ScopeBadge: Component<ScopeBadgeProps> = (props) => {
  const isLocal = (): boolean => props.scope === 'local'

  return (
    <span
      class={`
        inline-flex items-center
        px-1.5 py-px
        rounded-[var(--radius-sm)]
        text-[10px] font-medium
        leading-tight
        select-none
        ${
          isLocal()
            ? 'bg-[var(--accent-subtle)] text-[var(--accent-hover)]'
            : 'bg-[var(--gray-6)] text-[var(--gray-8)]'
        }
        ${props.class ?? ''}
      `}
    >
      {isLocal() ? 'Local' : 'Global'}
    </span>
  )
}
