/**
 * Model Browser Card
 *
 * Individual model card with provider icon, name, context window, price, and capability badges.
 * macOS-inspired dark design with category-colored capability pills.
 */

import { type Component, For, Show } from 'solid-js'
import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'
import { ProviderLogo } from '../../icons/ProviderLogo'
import { formatContextWindow, formatPricing } from './model-browser-helpers'
import type { BrowsableModel } from './model-browser-types'

interface ModelBrowserCardProps {
  model: BrowsableModel
  isSelected: boolean
  provider: LLMProviderConfig | undefined
  onSelect: () => void
  modelId: string
  providerId: string
}

/** Category-specific colors for capability badges */
const capabilityStyles: Record<string, { text: string; bg: string }> = {
  tools: { text: '#0A84FF', bg: 'rgba(10, 132, 255, 0.12)' },
  vision: { text: '#5E5CE6', bg: 'rgba(94, 92, 230, 0.12)' },
  reasoning: { text: '#F5A623', bg: 'rgba(245, 166, 35, 0.12)' },
  thinking: { text: '#34C759', bg: 'rgba(52, 199, 89, 0.12)' },
  free: { text: '#8E8E93', bg: 'rgba(255, 255, 255, 0.04)' },
}

export const ModelBrowserCard: Component<ModelBrowserCardProps> = (props) => (
  <button
    type="button"
    onClick={() => props.onSelect()}
    aria-current={props.isSelected ? 'true' : undefined}
    class="w-full text-left p-3.5 rounded-[10px] transition-all duration-150 group cursor-pointer"
    classList={{
      'bg-[#0F0F12] border border-[rgba(10,132,255,0.3)] shadow-[0_0_0_1px_rgba(10,132,255,0.1)]':
        props.isSelected,
      'bg-[#0F0F12] border border-[rgba(255,255,255,0.05)] hover:border-[rgba(255,255,255,0.1)]':
        !props.isSelected,
    }}
    style={{ contain: 'content' }}
  >
    {/* Header: icon + name */}
    <div class="flex items-start gap-2.5 mb-2.5">
      <Show when={props.provider}>
        {(p) => (
          <div class="w-7 h-7 rounded-[6px] flex items-center justify-center flex-shrink-0 bg-[rgba(255,255,255,0.04)] border border-[rgba(255,255,255,0.06)]">
            <ProviderLogo providerId={p().id} class="w-3.5 h-3.5" />
          </div>
        )}
      </Show>
      <div class="min-w-0 flex-1">
        <div class="flex items-center gap-1.5">
          <span class="text-[13px] font-medium text-[#F5F5F7] truncate leading-tight">
            {props.model.name}
          </span>
          <Show when={props.isSelected}>
            <span class="w-1.5 h-1.5 rounded-full bg-[var(--accent)] flex-shrink-0" />
          </Show>
        </div>
        <p class="text-[11px] text-[#8E8E93] truncate mt-0.5">{props.model.providerName}</p>
      </div>
    </div>

    {/* Stats row: context + pricing in mono */}
    <div class="flex items-center gap-2 text-[10px] text-[#8E8E93] mb-2.5 font-[var(--font-mono)]">
      <span>{formatContextWindow(props.model.contextWindow)} ctx</span>
      <Show when={props.model.pricing}>
        {(pricing) => (
          <>
            <span class="text-[rgba(255,255,255,0.1)]">&middot;</span>
            <span>{formatPricing(pricing())}</span>
          </>
        )}
      </Show>
    </div>

    {/* Capability badges — tiny pills with category colors */}
    <Show when={props.model.capabilities.length > 0}>
      <div class="flex flex-wrap gap-1">
        <For each={props.model.capabilities}>
          {(cap) => {
            const style = capabilityStyles[cap] ?? { text: '#8E8E93', bg: 'rgba(255,255,255,0.04)' }
            return (
              <span
                class="px-1.5 py-[1px] text-[9px] font-medium rounded-full capitalize font-[var(--font-mono)]"
                style={{ color: style.text, background: style.bg }}
              >
                {cap}
              </span>
            )
          }}
        </For>
      </div>
    </Show>
  </button>
)
