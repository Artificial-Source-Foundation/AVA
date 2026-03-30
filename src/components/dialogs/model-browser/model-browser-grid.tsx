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
  selectedProviderId?: string
  providers: LLMProviderConfig[]
  onSelect: (modelId: string, providerId: string) => void
}

export const ModelBrowserGrid: Component<ModelBrowserGridProps> = (props) => {
  const providerMap = () => {
    const map = new Map<string, LLMProviderConfig>()
    for (const p of props.providers) map.set(p.id, p)
    return map
  }

  /** Check if a model card should be highlighted as selected. */
  const isModelSelected = (model: BrowsableModel) => {
    if (model.id !== props.selectedModelId) return false
    // When a provider is tracked, disambiguate models with the same ID
    if (props.selectedProviderId) return model.providerId === props.selectedProviderId
    return true
  }

  return (
    <Show
      when={props.models.length > 0}
      fallback={
        <div class="py-12 text-center text-[13px] text-[#48484A]">No models match your filters</div>
      }
    >
      <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-2.5">
        <For each={props.models}>
          {(model) => (
            <ModelBrowserCard
              model={model}
              isSelected={isModelSelected(model)}
              provider={providerMap().get(model.providerId)}
              onSelect={() => props.onSelect(model.id, model.providerId)}
            />
          )}
        </For>
      </div>
    </Show>
  )
}
