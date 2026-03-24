/**
 * Providers Tab (Redesigned)
 *
 * Uses SettingsCard bento-grid pattern for consistency with other settings tabs.
 * Single card wrapping search + 2-column provider grid.
 */

import { Cloud } from 'lucide-solid'
import { type Component, createMemo, createSignal } from 'solid-js'
import type { ProviderModel } from '../../../../config/defaults/provider-defaults'
import { SettingsCard } from '../../SettingsCard'
import { SETTINGS_CARD_GAP } from '../../settings-constants'
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
  const [expandedIds, setExpandedIds] = createSignal<Set<string>>(new Set())
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
    <div class="grid grid-cols-1" style={{ gap: SETTINGS_CARD_GAP }}>
      <SettingsCard
        icon={Cloud}
        title="LLM Providers"
        description="Configure API keys and manage provider connections. Keys stored locally — never sent to AVA servers."
      >
        <ProvidersTabHeader
          connectedCount={connectedCount}
          searchQuery={searchQuery}
          onSearchChange={setSearchQuery}
        />

        <ProvidersTabGrid
          providers={filteredProviders()}
          expandedIds={expandedIds()}
          onToggleExpand={(id) =>
            setExpandedIds((prev) => {
              const next = new Set(prev)
              if (next.has(id)) next.delete(id)
              else next.add(id)
              return next
            })
          }
          onToggle={props.onToggle}
          onSaveApiKey={props.onSaveApiKey}
          onClearApiKey={props.onClearApiKey}
          onSetDefaultModel={props.onSetDefaultModel}
          onTestConnection={props.onTestConnection}
          onUpdateModels={props.onUpdateModels}
          onSaveBaseUrl={props.onSaveBaseUrl}
        />
      </SettingsCard>
    </div>
  )
}
