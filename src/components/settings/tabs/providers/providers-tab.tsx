/**
 * Providers Tab (Redesigned)
 *
 * 2-column card grid layout for managing all LLM providers.
 * Replaces the old accordion list design.
 */

import { type Component, createMemo, createSignal } from 'solid-js'
import type { ProviderModel } from '../../../../config/defaults/provider-defaults'
import { ProvidersTabGrid } from './providers-tab-grid'
import { ProvidersTabHeader } from './providers-tab-header'

export interface ProvidersTabProps {
  providers: import('../../../../config/defaults/provider-defaults').LLMProviderConfig[]
  onToggle?: (id: string, enabled: boolean) => void
  onSaveApiKey?: (id: string, key: string) => void
  onClearApiKey?: (id: string) => void
  onSetDefaultModel?: (providerId: string, modelId: string) => void
  onTestConnection?: (id: string) => void
  onUpdateModels?: (providerId: string, models: ProviderModel[]) => void
  onSaveBaseUrl?: (providerId: string, url: string) => void
}

export const ProvidersTab: Component<ProvidersTabProps> = (props) => {
  const [expandedId, setExpandedId] = createSignal<string | null>(null)
  const [searchQuery, setSearchQuery] = createSignal('')

  const connectedCount = createMemo(
    () => props.providers.filter((p) => p.status === 'connected').length
  )

  const filteredProviders = createMemo(() => {
    const q = searchQuery().toLowerCase()
    if (!q) return props.providers
    return props.providers.filter(
      (p) => p.name.toLowerCase().includes(q) || p.description.toLowerCase().includes(q)
    )
  })

  return (
    <div class="space-y-4">
      <ProvidersTabHeader
        connectedCount={connectedCount}
        searchQuery={searchQuery}
        onSearchChange={setSearchQuery}
      />

      <ProvidersTabGrid
        providers={filteredProviders()}
        expandedId={expandedId()}
        onExpand={setExpandedId}
        onToggle={props.onToggle}
        onSaveApiKey={props.onSaveApiKey}
        onClearApiKey={props.onClearApiKey}
        onSetDefaultModel={props.onSetDefaultModel}
        onTestConnection={props.onTestConnection}
        onUpdateModels={props.onUpdateModels}
        onSaveBaseUrl={props.onSaveBaseUrl}
      />

      <p class="text-[10px] text-[var(--text-muted)] text-center">
        Keys stored locally — Never sent to AVA servers
      </p>
    </div>
  )
}
