import { type Component, Show } from 'solid-js'
import type { AppSettings } from '../../stores/settings'
import { GeneralSection } from './settings-general-section'
import type { SettingsTab } from './settings-modal-config'
import { AdvancedTab } from './tabs/AdvancedTab'
import { AgentsTab } from './tabs/AgentsTab'
import { AppearanceTab } from './tabs/AppearanceTab'
import type { Keybinding } from './tabs/KeybindingsTab'
import { LLMTab } from './tabs/LLMTab'
import { type MCPServer, MCPServersTab } from './tabs/MCPServersTab'
import { PermissionsAndTrustTab } from './tabs/PermissionsAndTrustTab'
import { PluginsTab } from './tabs/PluginsTab'
import { ProvidersTab } from './tabs/providers/providers-tab'
import { SkillsSettingsTab } from './tabs/SkillsSettingsTab'

interface SettingsModalContentProps {
  activeTab: () => SettingsTab
  onSelectTab: (tab: SettingsTab) => void
  settings: () => AppSettings
  keybindings: () => Keybinding[]
  mcpServers: () => MCPServer[]
  onEditKeybinding: (id: string) => void
  onResetKeybinding: (id: string) => void
  onResetAllKeybindings: () => void
  onUpdateProvider: (id: string, patch: Partial<AppSettings['providers'][number]>) => void
  onUpdateAgent: (id: string, patch: Partial<AppSettings['agents'][number]>) => void
  onTestProvider: (id: string) => Promise<void>
  onRemoveMcpServer: (id: string) => void
  onAddMcpServer: () => void
  onRefreshMcpServers?: () => void
  onToggleMcpServer?: (name: string, enabled: boolean) => void
  isMcpLoading?: () => boolean
}

export const SettingsModalContent: Component<SettingsModalContentProps> = (props) => {
  const renderTab = (tab: SettingsTab) => {
    switch (tab) {
      case 'general':
        return (
          <GeneralSection
            keybindings={props.keybindings()}
            onEditKeybinding={props.onEditKeybinding}
            onResetKeybinding={props.onResetKeybinding}
            onResetAllKeybindings={props.onResetAllKeybindings}
          />
        )
      case 'appearance':
        return <AppearanceTab />
      case 'agents':
        return <AgentsTab />
      case 'providers':
        return (
          <ProvidersTab
            providers={props.settings().providers}
            onToggle={(id, enabled) => props.onUpdateProvider(id, { enabled })}
            onSaveApiKey={(id, key) =>
              props.onUpdateProvider(id, { apiKey: key, status: 'connected', enabled: true })
            }
            onClearApiKey={(id) =>
              props.onUpdateProvider(id, {
                apiKey: undefined,
                status: 'disconnected',
                enabled: false,
              })
            }
            onOAuthConnected={(id) =>
              props.onUpdateProvider(id, {
                apiKey: undefined,
                status: 'connected',
                enabled: true,
                error: undefined,
              })
            }
            onSetDefaultModel={(providerId, modelId) =>
              props.onUpdateProvider(providerId, { defaultModel: modelId })
            }
            onUpdateModels={(providerId, models) => {
              props.onUpdateProvider(providerId, {
                models,
                defaultModel: models.find((m) => m.isDefault)?.id || models[0]?.id,
              })
            }}
            onTestConnection={props.onTestProvider}
            onSaveBaseUrl={(id, url) => props.onUpdateProvider(id, { baseUrl: url || undefined })}
          />
        )
      case 'permissions-trust':
        return <PermissionsAndTrustTab />
      case 'advanced':
        return <AdvancedTab onSelectTab={props.onSelectTab} />
      case 'llm':
        return <LLMTab />
      case 'mcp':
        return (
          <MCPServersTab
            servers={props.mcpServers()}
            onRemove={props.onRemoveMcpServer}
            onAdd={props.onAddMcpServer}
            onRefresh={props.onRefreshMcpServers}
            onToggle={props.onToggleMcpServer}
            isLoading={props.isMcpLoading?.()}
          />
        )
      case 'plugins':
        return <PluginsTab />
      case 'skills':
        return <SkillsSettingsTab />
    }
  }

  return (
    <div
      class="max-w-full min-h-full"
      style={{
        'padding-top': '32px',
        'padding-left': '40px',
        'padding-right': '40px',
        'padding-bottom': '32px',
      }}
    >
      <Show when={props.activeTab()} keyed>
        {(tab) => <div class="settings-tab-panel">{renderTab(tab)}</div>}
      </Show>
    </div>
  )
}
