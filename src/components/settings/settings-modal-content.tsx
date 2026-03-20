import { type Component, Show } from 'solid-js'
import type { AppSettings } from '../../stores/settings'
import { AboutSection } from './settings-about-section'
import { GeneralSection } from './settings-general-section'
import type { SettingsTab } from './settings-modal-config'
import { AgentsTab } from './tabs/AgentsTab'
import { AppearanceTab } from './tabs/AppearanceTab'
import { BehaviorTab } from './tabs/BehaviorTab'
import { DeveloperTab } from './tabs/DeveloperTab'
import { type Keybinding, KeybindingsTab } from './tabs/KeybindingsTab'
import { LLMTab } from './tabs/LLMTab'
import { type MCPServer, MCPServersTab } from './tabs/MCPServersTab'
import { PermissionsAndTrustTab } from './tabs/PermissionsAndTrustTab'
import { PluginsTab } from './tabs/PluginsTab'
import { ProvidersTab } from './tabs/providers/providers-tab'
import { SkillsAndCommandsTab } from './tabs/SkillsAndCommandsTab'
import { TeamTab } from './tabs/TeamTab'

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
}

export const SettingsModalContent: Component<SettingsModalContentProps> = (props) => {
  return (
    <div
      class="max-w-full overflow-y-auto"
      style={{
        'padding-top': '32px',
        'padding-left': '40px',
        'padding-right': '40px',
        'padding-bottom': '32px',
      }}
    >
      <Show when={props.activeTab() === 'general'}>
        <GeneralSection />
      </Show>

      <Show when={props.activeTab() === 'appearance'}>
        <AppearanceTab />
      </Show>

      <Show when={props.activeTab() === 'behavior'}>
        <BehaviorTab />
      </Show>

      <Show when={props.activeTab() === 'shortcuts'}>
        <KeybindingsTab
          keybindings={props.keybindings()}
          onEdit={props.onEditKeybinding}
          onReset={props.onResetKeybinding}
          onResetAll={props.onResetAllKeybindings}
        />
      </Show>

      <Show when={props.activeTab() === 'providers'}>
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
      </Show>

      <Show when={props.activeTab() === 'permissions-trust'}>
        <PermissionsAndTrustTab />
      </Show>

      <Show when={props.activeTab() === 'agents'}>
        <AgentsTab />
      </Show>

      <Show when={props.activeTab() === 'team'}>
        <TeamTab />
      </Show>

      <Show when={props.activeTab() === 'llm'}>
        <LLMTab />
      </Show>

      <Show when={props.activeTab() === 'mcp'}>
        <MCPServersTab
          servers={props.mcpServers()}
          onRemove={props.onRemoveMcpServer}
          onAdd={props.onAddMcpServer}
        />
      </Show>

      <Show when={props.activeTab() === 'plugins'}>
        <PluginsTab />
      </Show>

      <Show when={props.activeTab() === 'skills-commands'}>
        <SkillsAndCommandsTab />
      </Show>

      <Show when={props.activeTab() === 'developer'}>
        <DeveloperTab />
      </Show>

      <Show when={props.activeTab() === 'about'}>
        <AboutSection />
      </Show>
    </div>
  )
}
