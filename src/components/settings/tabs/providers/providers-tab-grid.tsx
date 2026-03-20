/**
 * Providers Tab Grid
 *
 * 2-column grid of provider cards.
 */

import { type Component, For, Show } from 'solid-js'
import type {
  LLMProviderConfig,
  ProviderModel,
} from '../../../../config/defaults/provider-defaults'
import { ProviderCard } from './provider-card'

interface ProvidersTabGridProps {
  providers: LLMProviderConfig[]
  expandedId: string | null
  onExpand: (id: string | null) => void
  onToggle?: (id: string, enabled: boolean) => void
  onSaveApiKey?: (id: string, key: string) => void
  onClearApiKey?: (id: string) => void
  onSetDefaultModel?: (providerId: string, modelId: string) => void
  onTestConnection?: (id: string) => void
  onUpdateModels?: (providerId: string, models: ProviderModel[]) => void
  onSaveBaseUrl?: (providerId: string, url: string) => void
}

export const ProvidersTabGrid: Component<ProvidersTabGridProps> = (props) => (
  <Show
    when={props.providers.length > 0}
    fallback={
      <div class="py-6 text-center text-xs text-[var(--text-muted)]">
        No providers match your search
      </div>
    }
  >
    <div class="grid grid-cols-1 md:grid-cols-2 gap-2">
      <For each={props.providers}>
        {(provider) => (
          <ProviderCard
            provider={provider}
            isExpanded={props.expandedId === provider.id}
            onExpand={() => props.onExpand(props.expandedId === provider.id ? null : provider.id)}
            onToggle={(enabled) => props.onToggle?.(provider.id, enabled)}
            onSaveApiKey={(key) => props.onSaveApiKey?.(provider.id, key)}
            onClearApiKey={() => props.onClearApiKey?.(provider.id)}
            onSetDefaultModel={(modelId) => props.onSetDefaultModel?.(provider.id, modelId)}
            onTestConnection={() => props.onTestConnection?.(provider.id)}
            onUpdateModels={(models) => props.onUpdateModels?.(provider.id, models)}
            onSaveBaseUrl={(url) => props.onSaveBaseUrl?.(provider.id, url)}
          />
        )}
      </For>
    </div>
  </Show>
)
