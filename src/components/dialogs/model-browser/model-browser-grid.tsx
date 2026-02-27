/**
 * Model Browser Grid
 *
 * 3-column responsive grid of model cards.
 */

import { type Component, For, Show } from 'solid-js'
import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'
import { ModelBrowserCard } from './model-browser-card'
import type { BrowsableModel } from './model-browser-types'

interface ModelBrowserGridProps {
  models: BrowsableModel[]
  selectedModelId: string
  providers: LLMProviderConfig[]
  onSelect: (modelId: string) => void
}

export const ModelBrowserGrid: Component<ModelBrowserGridProps> = (props) => {
  const providerMap = () => {
    const map = new Map<string, LLMProviderConfig>()
    for (const p of props.providers) map.set(p.id, p)
    return map
  }

  return (
    <Show
      when={props.models.length > 0}
      fallback={
        <div class="py-8 text-center text-xs text-[var(--text-muted)]">
          No models match your filters
        </div>
      }
    >
      <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-2">
        <For each={props.models}>
          {(model) => (
            <ModelBrowserCard
              model={model}
              isSelected={props.selectedModelId === model.id}
              provider={providerMap().get(model.providerId)}
              onSelect={() => props.onSelect(model.id)}
            />
          )}
        </For>
      </div>
    </Show>
  )
}
