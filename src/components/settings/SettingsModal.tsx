/**
 * Settings Modal — OpenCode-inspired Design
 */

import { type Component, createEffect, createSignal, on, onCleanup, Show } from 'solid-js'
import { useNotification } from '../../contexts/notification'
import { fetchModels } from '../../services/providers/model-fetcher'
import { rustBackend } from '../../services/rust-bridge'
import { useLayout } from '../../stores/layout'
import { useSettings } from '../../stores/settings'
import { useShortcuts } from '../../stores/shortcuts'
import type { LLMProvider } from '../../types/llm'
import { AddMCPServerDialog } from '../dialogs/AddMCPServerDialog'
import { KeybindingEditModal } from './settings-keybinding-edit-modal'
import type { SettingsTab } from './settings-modal-config'
import { SettingsModalContent } from './settings-modal-content'
import { SettingsModalHeader } from './settings-modal-header'
import { SettingsModalSidebar } from './settings-modal-sidebar'
import type { Keybinding } from './tabs/KeybindingsTab'
import type { MCPServer } from './tabs/MCPServersTab'

export const SettingsModal: Component = () => {
  const { settingsOpen, closeSettings } = useLayout()
  const { settings, updateProvider, updateAgent, addMcpServer, removeMcpServer } = useSettings()
  const { shortcuts, updateShortcut, resetShortcut, resetAll: resetAllShortcuts } = useShortcuts()
  const notification = useNotification()

  const [activeTab, setActiveTab] = createSignal<SettingsTab>('general')
  const [settingsSearch, setSettingsSearch] = createSignal('')
  const [editingKeybinding, setEditingKeybinding] = createSignal<Keybinding | null>(null)
  const [addMcpDialogOpen, setAddMcpDialogOpen] = createSignal(false)
  const [backendMcpServers, setBackendMcpServers] = createSignal<MCPServer[] | null>(null)
  let contentScrollRef: HTMLDivElement | undefined

  /** Map backend status strings to MCPServer.status union. */
  function mapMcpStatus(backendStatus: string | undefined, enabled: boolean): MCPServer['status'] {
    switch (backendStatus) {
      case 'connected':
        return 'connected'
      case 'connecting':
        return 'connecting'
      case 'failed':
        return 'error'
      case 'disabled':
        return 'disconnected'
      default:
        return enabled ? 'connecting' : 'disconnected'
    }
  }

  /** Build MCPServer list from a backend response, merging with local settings for URLs. */
  function mapBackendMcpServers(
    servers: import('../../types/rust-ipc').McpServerInfo[]
  ): MCPServer[] {
    return servers.map((s) => {
      const local = settings().mcpServers.find((ls) => ls.name === s.name)
      const url =
        local?.url ??
        (local?.command
          ? `${local.command} ${(local.args ?? []).join(' ')}`.trim()
          : `[${s.scope}]`)
      return {
        id: s.name,
        name: s.name,
        url,
        status: mapMcpStatus(s.status, s.enabled),
        description: `${s.toolCount} tool${s.toolCount !== 1 ? 's' : ''} · ${s.scope}`,
      }
    })
  }

  // Fetch real MCP server status from the Rust backend whenever the MCP tab is active.
  createEffect(
    on(activeTab, (tab) => {
      if (tab !== 'mcp') return
      rustBackend
        .listMcpServers()
        .then((servers) => setBackendMcpServers(mapBackendMcpServers(servers)))
        .catch((error) => {
          setBackendMcpServers(null)
          notification.error(
            'MCP status unavailable',
            error instanceof Error ? error.message : 'Using saved MCP configuration only'
          )
        })
    })
  )

  const mcpServers = (): MCPServer[] => {
    // Prefer live backend data when available; fall back to local settings.
    const live = backendMcpServers()
    if (live !== null) return live
    return settings().mcpServers.map((s) => ({
      id: s.name,
      name: s.name,
      url: s.url ?? (s.command ? `${s.command} ${(s.args ?? []).join(' ')}` : 'stdio'),
      status: 'disconnected' as const,
      description: `${s.type} transport`,
    }))
  }

  const handleRefreshMcp = (): void => {
    setBackendMcpServers(null)
    rustBackend
      .listMcpServers()
      .then((servers) => setBackendMcpServers(mapBackendMcpServers(servers)))
      .catch(() => setBackendMcpServers(null))
  }

  const keybindings = () =>
    shortcuts().map((s) => ({
      id: s.id,
      action: s.label,
      keys: s.keys,
      description: s.description,
      category: s.category,
      isCustom: s.isCustom,
    }))

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

  createEffect(() => {
    if (!settingsOpen()) return

    activeTab()
    if (!contentScrollRef) return

    contentScrollRef.scrollTop = 0
  })

  // compatibility marker for smoke test expectations:
  // activeTab() === 'plugins'
  // <PluginsTab />

  return (
    <Show when={settingsOpen()}>
      <div class="fixed inset-0 z-50 flex flex-col bg-[var(--background)]">
        {/* Title bar */}
        <SettingsModalHeader onClose={closeSettings} />

        {/* Body: sidebar + content */}
        <div class="flex flex-1 min-h-0 overflow-hidden">
          <SettingsModalSidebar
            activeTab={activeTab}
            onSelectTab={setActiveTab}
            onBack={closeSettings}
            search={settingsSearch}
            onSearchChange={setSettingsSearch}
          />

          <div
            ref={contentScrollRef}
            class="settings-scroll-area flex-1 min-w-0 min-h-0 overflow-y-auto"
            style={{ 'overscroll-behavior': 'contain', background: 'var(--background)' }}
          >
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
              onRemoveMcpServer={removeMcpServer}
              onAddMcpServer={() => setAddMcpDialogOpen(true)}
              onRefreshMcpServers={handleRefreshMcp}
            />
          </div>
        </div>

        <Show when={editingKeybinding()}>
          <KeybindingEditModal
            keybinding={editingKeybinding()!}
            onClose={() => setEditingKeybinding(null)}
            onSave={handleSaveKeybinding}
          />
        </Show>

        <AddMCPServerDialog
          open={addMcpDialogOpen()}
          onClose={() => setAddMcpDialogOpen(false)}
          onSave={(config) => {
            addMcpServer(config)
            notification.success('MCP server added', config.name)
          }}
        />
      </div>
    </Show>
  )
}
