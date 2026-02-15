import { type Component, createSignal, For } from 'solid-js'
import { ProviderRow } from './provider-row'
import type { ProvidersTabProps } from './providers-tab-types'

export type { LLMProviderConfig, ProviderModel } from '../../../config/defaults/provider-defaults'
export type { ProvidersTabProps } from './providers-tab-types'

export const ProvidersTab: Component<ProvidersTabProps> = (props) => {
  const [expandedId, setExpandedId] = createSignal<string | null>(null)

  const connectedCount = () => props.providers.filter((p) => p.status === 'connected').length

  return (
    <div class="space-y-4">
      <p class="text-[10px] text-[var(--text-muted)]">
        {connectedCount() > 0 ? (
          <span class="text-[var(--success)]">{connectedCount()} connected</span>
        ) : (
          'Configure your AI providers'
        )}
      </p>

      <div class="space-y-0.5">
        <For each={props.providers}>
          {(provider) => (
            <ProviderRow
              provider={provider}
              isExpanded={expandedId() === provider.id}
              onExpand={() => setExpandedId(expandedId() === provider.id ? null : provider.id)}
              onToggle={(enabled) => props.onToggle?.(provider.id, enabled)}
              onSaveApiKey={(key) => props.onSaveApiKey?.(provider.id, key)}
              onClearApiKey={() => props.onClearApiKey?.(provider.id)}
              onSetDefaultModel={(modelId) => props.onSetDefaultModel?.(provider.id, modelId)}
              onTestConnection={() => props.onTestConnection?.(provider.id)}
              onUpdateModels={(models) => props.onUpdateModels?.(provider.id, models)}
            />
          )}
        </For>
      </div>

      <p class="text-[10px] text-[var(--text-muted)] text-center">
        Keys stored locally - Never sent to AVA servers
      </p>
    </div>
  )
}

export { defaultProviders } from '../../../config/defaults/provider-defaults'
