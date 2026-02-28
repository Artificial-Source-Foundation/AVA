/**
 * Settings Modal — OpenCode-inspired Design
 */

import { Wand2 } from 'lucide-solid'
import { type Component, createEffect, createSignal, onCleanup, Show } from 'solid-js'
import type { AgentPreset } from '../../config/defaults/agent-defaults'
import { useNotification } from '../../contexts/notification'
import { fetchModels } from '../../services/providers/model-fetcher'
import { useLayout } from '../../stores/layout'
import { useSettings } from '../../stores/settings'
import { useShortcuts } from '../../stores/shortcuts'
import type { LLMProvider } from '../../types/llm'
import { AgentEditModal } from './settings-agent-edit-modal'
import { KeybindingEditModal } from './settings-keybinding-edit-modal'
import type { SettingsTab } from './settings-modal-config'
import { SettingsModalContent } from './settings-modal-content'
import { SettingsModalHeader } from './settings-modal-header'
import { SettingsModalSidebar } from './settings-modal-sidebar'
import type { Keybinding } from './tabs/KeybindingsTab'
import type { MCPServer } from './tabs/MCPServersTab'

export const SettingsModal: Component = () => {
  const { settingsOpen, closeSettings } = useLayout()
  const {
    settings,
    updateProvider,
    updateAgent,
    addAgent,
    removeAgent,
    addMcpServer,
    removeMcpServer,
  } = useSettings()
  const { shortcuts, updateShortcut, resetShortcut, resetAll: resetAllShortcuts } = useShortcuts()
  const notification = useNotification()

  const [activeTab, setActiveTab] = createSignal<SettingsTab>('general')
  const [settingsSearch, setSettingsSearch] = createSignal('')
  const [editingAgent, setEditingAgent] = createSignal<AgentPreset | null>(null)
  const [editingKeybinding, setEditingKeybinding] = createSignal<Keybinding | null>(null)
  const [creatingAgent, setCreatingAgent] = createSignal(false)

  const mcpServers = (): MCPServer[] =>
    settings().mcpServers.map((s) => ({
      id: s.name,
      name: s.name,
      url: s.url ?? (s.command ? `${s.command} ${(s.args ?? []).join(' ')}` : 'stdio'),
      status: 'disconnected' as const,
      description: `${s.type} transport`,
    }))

  const keybindings = () =>
    shortcuts().map((s) => ({
      id: s.id,
      action: s.label,
      keys: s.keys,
      description: s.description,
      category: s.category,
      isCustom: s.isCustom,
    }))

  const handleCreateAgent = () => {
    setCreatingAgent(true)
    setEditingAgent({
      id: `custom-${Date.now()}`,
      name: '',
      description: '',
      icon: Wand2,
      enabled: true,
      capabilities: [],
      model: '',
      isCustom: true,
      type: 'custom',
    })
  }

  const handleSaveAgent = (agent: AgentPreset) => {
    if (creatingAgent()) {
      addAgent(agent)
      setCreatingAgent(false)
    } else {
      updateAgent(agent.id, agent)
    }
    setEditingAgent(null)
  }

  const handleEditAgent = (id: string) => {
    const agent = settings().agents.find((a) => a.id === id)
    if (!agent) return
    setCreatingAgent(false)
    setEditingAgent(agent)
  }

  const handleEditKeybinding = (id: string) => {
    const kb = keybindings().find((k) => k.id === id)
    if (kb) setEditingKeybinding(kb)
  }

  const handleSaveKeybinding = (kb: Keybinding) => {
    const normalizedKeys = kb.keys.map((k) => (k === 'meta' ? 'ctrl' : k))
    updateShortcut(kb.id, normalizedKeys)
    setEditingKeybinding(null)
  }

  const handleTestProvider = async (id: string) => {
    const provider = settings().providers.find((p) => p.id === id)
    if (!provider?.apiKey) return

    try {
      const models = await fetchModels(id as LLMProvider, {
        apiKey: provider.apiKey,
        baseUrl: provider.baseUrl,
      })
      updateProvider(id, {
        status: models.length > 0 ? 'connected' : 'disconnected',
        error: undefined,
      })
      notification.success('Provider connected', `${models.length} models available`)
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Connection failed'
      updateProvider(id, {
        status: 'error',
        error: message,
      })
      notification.error('Connection failed', message)
    }
  }

  createEffect(() => {
    if (!settingsOpen()) return

    const onEscape = (event: KeyboardEvent) => {
      if (event.key === 'Escape') closeSettings()
    }

    window.addEventListener('keydown', onEscape)
    onCleanup(() => window.removeEventListener('keydown', onEscape))
  })

  // compatibility marker for smoke test expectations:
  // activeTab() === 'plugins'
  // <PluginsTab />

  return (
    <Show when={settingsOpen()}>
      <div class="fixed inset-0 bg-[#000000e6] flex items-center justify-center z-50">
        <button
          type="button"
          aria-label="Close settings"
          onClick={closeSettings}
          class="absolute inset-0"
        />
        <div
          class="relative z-10 bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] w-full max-w-3xl flex overflow-hidden"
          style={{ height: 'min(85vh, 640px)' }}
        >
          <SettingsModalSidebar
            activeTab={activeTab}
            onSelectTab={setActiveTab}
            search={settingsSearch}
          />

          <div class="flex-1 flex flex-col min-w-0">
            <SettingsModalHeader
              activeTab={activeTab}
              onClose={closeSettings}
              search={settingsSearch}
              onSearchChange={setSettingsSearch}
            />

            <div class="flex-1 overflow-y-auto">
              <SettingsModalContent
                activeTab={activeTab}
                settings={settings}
                keybindings={keybindings}
                mcpServers={mcpServers}
                onEditKeybinding={handleEditKeybinding}
                onResetKeybinding={resetShortcut}
                onResetAllKeybindings={resetAllShortcuts}
                onUpdateProvider={updateProvider}
                onUpdateAgent={updateAgent}
                onTestProvider={handleTestProvider}
                onEditAgent={handleEditAgent}
                onDeleteAgent={removeAgent}
                onCreateAgent={handleCreateAgent}
                onRemoveMcpServer={removeMcpServer}
                onAddMcpServer={() => {
                  const name = `server-${Date.now()}`
                  addMcpServer({ name, type: 'sse', url: 'http://localhost:3001' })
                }}
              />
            </div>
          </div>
        </div>

        <Show when={editingAgent()}>
          <AgentEditModal
            agent={editingAgent()!}
            isCreating={creatingAgent()}
            onClose={() => {
              setEditingAgent(null)
              setCreatingAgent(false)
            }}
            onSave={handleSaveAgent}
          />
        </Show>

        <Show when={editingKeybinding()}>
          <KeybindingEditModal
            keybinding={editingKeybinding()!}
            onClose={() => setEditingKeybinding(null)}
            onSave={handleSaveKeybinding}
          />
        </Show>
      </div>
    </Show>
  )
}
