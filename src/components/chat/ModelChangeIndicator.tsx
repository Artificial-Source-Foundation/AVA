/**
 * Model Change Indicator
 *
 * Thin separator shown between messages when the model changes.
 */

import { Repeat } from 'lucide-solid'
import type { Component } from 'solid-js'

interface ModelChangeIndicatorProps {
  from: string
  to: string
}

/** Trim model IDs to display-friendly names */
function shortModel(id: string): string {
  // Strip common prefixes like "anthropic/", "openai/"
  const stripped = id.includes('/') ? id.split('/').pop()! : id
  return stripped.length > 25 ? `${stripped.slice(0, 22)}...` : stripped
}

export const ModelChangeIndicator: Component<ModelChangeIndicatorProps> = (props) => (
  <div class="flex items-center gap-2 py-1 px-4 my-0.5">
    <div class="flex-1 h-px bg-[var(--border-subtle)]" />
    <span class="flex items-center gap-1.5 text-[10px] text-[var(--text-muted)]">
      <Repeat class="w-3 h-3" />
      <span class="font-[var(--font-ui-mono)]">{shortModel(props.from)}</span>
      <span>&rarr;</span>
      <span class="font-[var(--font-ui-mono)]">{shortModel(props.to)}</span>
    </span>
    <div class="flex-1 h-px bg-[var(--border-subtle)]" />
  </div>
)
