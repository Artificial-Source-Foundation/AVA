/**
 * Model Browser Header
 *
 * Displays the currently selected model with details.
 */

import type { Accessor, Component } from 'solid-js'
import { Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'
import { formatContextWindow, formatPricingFull } from './model-browser-helpers'
import type { BrowsableModel } from './model-browser-types'

interface ModelBrowserHeaderProps {
  selectedModel: BrowsableModel | undefined
  provider: LLMProviderConfig | undefined
  modelCount: Accessor<number>
}

export const ModelBrowserHeader: Component<ModelBrowserHeaderProps> = (props) => (
  <div class="flex items-center justify-between pb-3 border-b border-[var(--border-subtle)]">
    <Show
      when={props.selectedModel}
      fallback={<div class="text-xs text-[var(--text-muted)]">No model selected</div>}
    >
      {(model) => (
        <div class="flex items-center gap-3 min-w-0">
          <Show when={props.provider}>
            {(p) => (
              <div
                class="w-9 h-9 rounded-[var(--radius-md)] flex items-center justify-center flex-shrink-0"
                style={{ background: 'var(--alpha-white-5)' }}
              >
                <Dynamic component={p().icon} class="w-4 h-4 text-[var(--text-secondary)]" />
              </div>
            )}
          </Show>
          <div class="min-w-0">
            <p class="text-sm font-medium text-[var(--text-primary)] truncate">{model().name}</p>
            <p class="text-[10px] text-[var(--text-muted)] truncate">
              {model().providerName} · {formatContextWindow(model().contextWindow)} ctx
              <Show when={model().pricing}>
                {(pricing) => <> · {formatPricingFull(pricing())}</>}
              </Show>
            </p>
          </div>
        </div>
      )}
    </Show>
    <span class="text-[10px] text-[var(--text-muted)] flex-shrink-0 ml-3">
      {props.modelCount()} models
    </span>
  </div>
)
