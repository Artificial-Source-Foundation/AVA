/**
 * Model Selector Button
 *
 * Button that opens the Model Browser dialog.
 */

import { ChevronDown } from 'lucide-solid'
import type { Accessor, Component } from 'solid-js'

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

export interface ModelSelectorProps {
  onToggle: () => void
  currentModelDisplay: Accessor<string>
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const ModelSelector: Component<ModelSelectorProps> = (props) => (
  <button
    type="button"
    onClick={props.onToggle}
    class="
      flex items-center gap-1 px-2 py-1
      text-[11px] text-[var(--text-secondary)]
      bg-[var(--surface-raised)]
      border border-[var(--border-subtle)]
      rounded-[var(--radius-md)]
      hover:border-[var(--accent-muted)]
      transition-colors
    "
  >
    <ChevronDown class="w-3 h-3" />
    <span class="truncate max-w-[140px]">{props.currentModelDisplay()}</span>
  </button>
)
