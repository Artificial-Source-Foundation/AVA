/**
 * Model Browser Header
 *
 * Displays the currently selected model with details.
 * Compact row: provider icon + name (bold) + provider + pricing in mono.
 */

import type { Accessor, Component } from 'solid-js'
import { Show } from 'solid-js'
import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'
import { ProviderLogo } from '../../icons/ProviderLogo'
import { formatContextWindow, formatPricingFull } from './model-browser-helpers'
import type { BrowsableModel } from './model-browser-types'

interface ModelBrowserHeaderProps {
  selectedModel: BrowsableModel | undefined
  provider: LLMProviderConfig | undefined
  modelCount: Accessor<number>
}

export const ModelBrowserHeader: Component<ModelBrowserHeaderProps> = (props) => (
  <div class="flex items-center justify-between pb-3 mb-1 border-b border-[rgba(255,255,255,0.06)]">
    <Show
      when={props.selectedModel}
      fallback={
        <div class="text-xs text-[var(--text-secondary)] font-[var(--font-mono)]">
          No model selected
        </div>
      }
    >
      {(model) => (
        <div class="flex items-center gap-3 min-w-0">
          <Show when={props.provider}>
            {(p) => (
              <div class="w-8 h-8 rounded-[8px] flex items-center justify-center flex-shrink-0 bg-[rgba(255,255,255,0.04)] border border-[rgba(255,255,255,0.06)]">
                <ProviderLogo providerId={p().id} class="w-4 h-4" />
              </div>
            )}
          </Show>
          <div class="min-w-0">
            <p class="text-[15px] font-semibold text-[#F5F5F7] truncate leading-tight">
              {model().name}
            </p>
            <p class="text-[11px] text-[var(--text-secondary)] truncate mt-0.5 font-[var(--font-mono)]">
              {model().providerName}
              <Show when={model().contextWindow}>
                {' \u00B7 '}
                {formatContextWindow(model().contextWindow)} ctx
              </Show>
              <Show when={model().pricing}>
                {(pricing) => (
                  <>
                    {' \u00B7 '}
                    {formatPricingFull(pricing())}
                  </>
                )}
              </Show>
            </p>
          </div>
        </div>
      )}
    </Show>
    <span class="text-[11px] text-[var(--text-secondary)] flex-shrink-0 ml-3 font-[var(--font-mono)]">
      {props.modelCount()} models
    </span>
  </div>
)
