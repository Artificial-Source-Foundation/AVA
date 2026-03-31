import { type Component, Show } from 'solid-js'
import type { AppSettings } from '../../stores/settings'
import { AboutSection } from './settings-about-section'
import { GeneralSection } from './settings-general-section'
import type { SettingsTab } from './settings-modal-config'
import { AgentsTab } from './tabs/AgentsTab'
import { AppearanceTab } from './tabs/AppearanceTab'
import { BehaviorTab } from './tabs/BehaviorTab'
import { DeveloperTab } from './tabs/DeveloperTab'
import { HqTab } from './tabs/HqTab'
import { type Keybinding, KeybindingsTab } from './tabs/KeybindingsTab'
import { LLMTab } from './tabs/LLMTab'
import { type MCPServer, MCPServersTab } from './tabs/MCPServersTab'
import { PermissionsAndTrustTab } from './tabs/PermissionsAndTrustTab'
import { PluginsTab } from './tabs/PluginsTab'
import { ProvidersTab } from './tabs/providers/providers-tab'
import { SkillsAndCommandsTab } from './tabs/SkillsAndCommandsTab'
import { SkillsSettingsTab } from './tabs/SkillsSettingsTab'
import { UsageTab } from './tabs/UsageTab'

interface SettingsModalContentProps {
  activeTab: () => SettingsTab
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
}

export const SettingsModalContent: Component<SettingsModalContentProps> = (props) => {
  const renderTab = (tab: SettingsTab) => {
    switch (tab) {
      case 'general':
        return <GeneralSection />
      case 'appearance':
        return <AppearanceTab />
      case 'behavior':
        return <BehaviorTab />
      case 'shortcuts':
        return (
          <KeybindingsTab
            keybindings={props.keybindings()}
            onEdit={props.onEditKeybinding}
            onReset={props.onResetKeybinding}
            onResetAll={props.onResetAllKeybindings}
          />
        )
      case 'providers':
        return (
          <ProvidersTab
            providers={props.settings().providers}
            onToggle={(id, enabled) => props.onUpdateProvider(id, { enabled })}
            onSaveApiKey={(id, key) =>
              props.onUpdateProvider(id, { apiKey: key, status: 'connected', enabled: true })
            }
            onClearApiKey={(id) =>
              props.onUpdateProvider(id, { apiKey: undefined, status: 'disconnected' })
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
      case 'usage':
        return <UsageTab />
      case 'permissions-trust':
        return <PermissionsAndTrustTab />
      case 'agents':
        return <AgentsTab />
      case 'hq':
        return <HqTab />
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
          />
        )
      case 'plugins':
        return <PluginsTab />
      case 'skills':
        return <SkillsSettingsTab />
      case 'skills-commands':
        return <SkillsAndCommandsTab />
      case 'developer':
        return <DeveloperTab />
      case 'about':
        return <AboutSection />
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
