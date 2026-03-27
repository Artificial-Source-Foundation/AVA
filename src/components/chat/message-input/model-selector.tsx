/**
 * Model Selector Button
 *
 * Pill-shaped button that opens the Model Browser dialog.
 * Shows "Provider | Model" with chevron-down icon.
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
    onClick={() => props.onToggle()}
    class="
      flex items-center gap-1 px-2.5 py-1
      text-[var(--text-xs)] text-[var(--text-secondary)]
      bg-[var(--alpha-white-5)]
      rounded-full
      hover:bg-[var(--alpha-white-8)]
      transition-colors
    "
  >
    <ChevronDown class="w-3 h-3" />
    <span class="truncate max-w-[200px]">{props.currentModelDisplay()}</span>
  </button>
)
