/**
 * Model Selector Dropdown
 *
 * Dropdown button that shows enabled providers and their models.
 */

import { ChevronDown } from 'lucide-solid'
import { type Accessor, type Component, For, Show } from 'solid-js'
import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

export interface ModelSelectorProps {
  isOpen: Accessor<boolean>
  onToggle: () => void
  onClose: () => void
  onSelect: (modelId: string) => void
  currentModelDisplay: Accessor<string>
  selectedModel: Accessor<string>
  enabledProviders: Accessor<LLMProviderConfig[]>
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const ModelSelector: Component<ModelSelectorProps> = (props) => (
  <div class="relative">
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

    {/* Model dropdown */}
    <Show when={props.isOpen()}>
      {/* biome-ignore lint/a11y/noStaticElementInteractions: backdrop click to close dropdown */}
      {/* biome-ignore lint/a11y/useKeyWithClickEvents: backdrop does not need keyboard interaction */}
      <div class="fixed inset-0 z-40" onClick={props.onClose} />
      <div
        class="absolute bottom-full left-0 mb-1 z-50 w-64 max-h-72 overflow-y-auto bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-lg)] shadow-xl"
        style={{ transform: 'translateZ(0)' }}
      >
        <For each={props.enabledProviders()}>
          {(provider) => (
            <div>
              <div class="px-3 py-1.5 text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider sticky top-0 bg-[var(--surface-overlay)]">
                {provider.name}
              </div>
              <For each={provider.models}>
                {(model) => (
                  <button
                    type="button"
                    onClick={() => props.onSelect(model.id)}
                    class={`
                      w-full text-left px-3 py-1.5
                      text-xs transition-colors
                      ${
                        props.selectedModel() === model.id
                          ? 'text-[var(--accent)] bg-[var(--accent-subtle)]'
                          : 'text-[var(--text-secondary)] hover:bg-[var(--alpha-white-5)] hover:text-[var(--text-primary)]'
                      }
                    `}
                  >
                    {model.name}
                  </button>
                )}
              </For>
            </div>
          )}
        </For>
        <Show when={props.enabledProviders().length === 0}>
          <div class="px-3 py-4 text-center text-xs text-[var(--text-muted)]">
            No providers configured
          </div>
        </Show>
      </div>
    </Show>
  </div>
)
