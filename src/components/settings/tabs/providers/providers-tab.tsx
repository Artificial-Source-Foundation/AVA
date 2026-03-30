/**
 * Providers Tab — Pencil macOS-inspired design.
 *
 * Page title + 2-column provider grid cards.
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
    <div style={{ display: 'flex', 'flex-direction': 'column', gap: '32px' }}>
      {/* Page title */}
      <h1
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '22px',
          'font-weight': '600',
          color: '#F5F5F7',
        }}
      >
        Providers
      </h1>

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
    </div>
  )
}
