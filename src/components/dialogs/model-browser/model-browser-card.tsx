/**
 * Model Browser Card
 *
 * Individual model card with provider icon, name, context window, price, and capability badges.
 */

import { type Component, For, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'
import { formatContextWindow, formatPricing } from './model-browser-helpers'
import type { BrowsableModel } from './model-browser-types'

interface ModelBrowserCardProps {
  model: BrowsableModel
  isSelected: boolean
  provider: LLMProviderConfig | undefined
  onSelect: () => void
}

const capabilityColors: Record<string, string> = {
  reasoning: 'text-[var(--warning)] bg-[var(--warning)]/10',
  thinking: 'text-[var(--accent)] bg-[var(--accent)]/10',
  tools: 'text-[var(--info)] bg-[var(--info)]/10',
  vision: 'text-[var(--success)] bg-[var(--success)]/10',
  free: 'text-[var(--text-muted)] bg-[var(--alpha-white-5)]',
}

export const ModelBrowserCard: Component<ModelBrowserCardProps> = (props) => (
  <button
    type="button"
    onClick={() => props.onSelect()}
    class={`
      w-full text-left p-3 rounded-[var(--radius-lg)]
      border transition-colors duration-150
      hover:bg-[var(--alpha-white-5)]
      ${
        props.isSelected
          ? 'border-[var(--accent)] bg-[var(--accent)]/5'
          : 'border-[var(--border-subtle)] bg-[var(--surface-raised)]'
      }
    `}
    style={{ contain: 'content' }}
  >
    {/* Header: icon + name */}
    <div class="flex items-start gap-2.5 mb-2">
      <Show when={props.provider}>
        {(p) => (
          <div
            class="w-7 h-7 rounded-[var(--radius-md)] flex items-center justify-center flex-shrink-0"
            style={{ background: 'var(--alpha-white-5)' }}
          >
            <Dynamic component={p().icon} class="w-3.5 h-3.5 text-[var(--text-secondary)]" />
          </div>
        )}
      </Show>
      <div class="min-w-0 flex-1">
        <div class="flex items-center gap-1.5">
          <span class="text-xs font-medium text-[var(--text-primary)] truncate">
            {props.model.name}
          </span>
          <Show when={props.isSelected}>
            <span class="w-1.5 h-1.5 rounded-full bg-[var(--accent)] flex-shrink-0" />
          </Show>
        </div>
        <p class="text-[10px] text-[var(--text-muted)] truncate">{props.model.providerName}</p>
      </div>
    </div>

    {/* Meta row */}
    <div class="flex items-center gap-2 text-[10px] text-[var(--text-muted)] mb-2">
      <span>{formatContextWindow(props.model.contextWindow)} ctx</span>
      <Show when={props.model.pricing}>
        {(pricing) => (
          <>
            <span class="text-[var(--border-default)]">·</span>
            <span>{formatPricing(pricing())}</span>
          </>
        )}
      </Show>
    </div>

    {/* Capability badges */}
    <Show when={props.model.capabilities.length > 0}>
      <div class="flex flex-wrap gap-1">
        <For each={props.model.capabilities}>
          {(cap) => (
            <span
              class={`px-1.5 py-0.5 text-[9px] font-medium rounded-full capitalize ${capabilityColors[cap] ?? 'text-[var(--text-muted)] bg-[var(--alpha-white-5)]'}`}
            >
              {cap}
            </span>
          )}
        </For>
      </div>
    </Show>
  </button>
)
