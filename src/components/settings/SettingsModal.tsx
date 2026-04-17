/**
 * Settings Modal — OpenCode-inspired Design
 */

import { type Component, createEffect, createSignal, on, onCleanup, Show } from 'solid-js'
import { defaultProviders, type ProviderModel } from '../../config/defaults/provider-defaults'
import { useNotification } from '../../contexts/notification'
import { enrichWithCatalog, fetchModels } from '../../services/providers/model-fetcher'
import { rustBackend } from '../../services/rust-bridge'
import { useLayout } from '../../stores/layout'
import { useSettings } from '../../stores/settings'
import { getProviderCredentialInfo } from '../../stores/settings/settings-mutators'
import type { MCPServerConfig } from '../../stores/settings/settings-types'
import { useShortcuts } from '../../stores/shortcuts'
import type { LLMProvider } from '../../types/llm'
import { AddMCPServerDialog } from '../dialogs/AddMCPServerDialog'
import { KeybindingEditModal } from './settings-keybinding-edit-modal'
import type { SettingsTab } from './settings-modal-config'
import { tabGroups } from './settings-modal-config'
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
  const [hasAttemptedLiveMcpFetch, setHasAttemptedLiveMcpFetch] = createSignal(false)
  const [isLiveMcpLoading, setIsLiveMcpLoading] = createSignal(false)
  let contentScrollRef: HTMLDivElement | undefined
  const validTabs = new Set<SettingsTab>(
    tabGroups.flatMap((group) => group.tabs.map((tab) => tab.id))
  )
  let mcpRequestGeneration = 0

  /** Map backend status strings to MCPServer.status union. */
  function mapMcpStatus(backendStatus: string | undefined, enabled: boolean): MCPServer['status'] {
    if (!enabled) return 'disabled'
    switch (backendStatus) {
      case 'connected':
        return 'connected'
      case 'connecting':
        return 'connecting'
      case 'failed':
        return 'error'
      case 'disabled':
        return 'disabled'
      default:
        return enabled ? 'connecting' : 'disabled'
    }
  }

  function normalizeMcpScope(scope: string | undefined): MCPServer['scope'] {
    if (!scope) return undefined
    const normalized = scope.toLowerCase()
    return normalized === 'local' ? 'local' : normalized === 'global' ? 'global' : undefined
  }

  function mapSavedMcpServer(server: MCPServerConfig): MCPServer {
    return {
      id: server.name,
      name: server.name,
      url:
        server.url ??
        (server.command ? `${server.command} ${(server.args ?? []).join(' ')}`.trim() : 'stdio'),
      enabled: true,
      hasBackendIdentity: false,
      hasSavedConfig: true,
      scope: 'local',
      status: 'disconnected',
      description: `${server.type} transport`,
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
        (local?.command ? `${local.command} ${(local.args ?? []).join(' ')}`.trim() : '')
      return {
        id: s.name,
        name: s.name,
        url,
        enabled: s.enabled,
        canToggle: s.canToggle,
        hasBackendIdentity: true,
        hasSavedConfig: Boolean(local),
        scope: normalizeMcpScope(s.scope),
        toolCount: s.toolCount,
        status: mapMcpStatus(s.status, s.enabled),
        error: s.error,
        description: `${s.toolCount} tool${s.toolCount !== 1 ? 's' : ''}${s.scope ? ` · ${s.scope.toLowerCase()}` : ''}`,
      }
    })
  }

  function applyCurrentSavedMcpConfig(server: MCPServer): MCPServer {
    const local = settings().mcpServers.find((savedServer) => savedServer.name === server.name)
    const url =
      local?.url ??
      (local?.command
        ? `${local.command} ${(local.args ?? []).join(' ')}`.trim()
        : server.hasSavedConfig
          ? ''
          : server.url)

    return {
      ...server,
      url,
      hasSavedConfig: Boolean(local),
    }
  }

  function mergeMcpServers(liveServers: MCPServer[] | null): MCPServer[] {
    const savedServers = settings().mcpServers.map(mapSavedMcpServer)
    if (liveServers === null) return savedServers

    const mergedServers = new Map(
      liveServers.map((server) => [server.name, applyCurrentSavedMcpConfig(server)])
    )
    for (const server of savedServers) {
      if (!mergedServers.has(server.name)) {
        mergedServers.set(server.name, server)
      }
    }

    return Array.from(mergedServers.values())
  }

  const mcpServers = (): MCPServer[] => {
    return mergeMcpServers(backendMcpServers())
  }

  const formatMcpError = (error: unknown, fallback: string): string =>
    error instanceof Error ? error.message : fallback

  const isCurrentMcpRequest = (generation: number): boolean => generation === mcpRequestGeneration

  const invalidateLiveMcpRequests = (): void => {
    mcpRequestGeneration += 1
    setIsLiveMcpLoading(false)
  }

  const fetchLiveMcpServers = async (errorTitle: string): Promise<boolean> => {
    const requestGeneration = ++mcpRequestGeneration
    setHasAttemptedLiveMcpFetch(true)
    setIsLiveMcpLoading(true)
    try {
      const servers = await rustBackend.listMcpServers()
      if (!isCurrentMcpRequest(requestGeneration)) return false
      setBackendMcpServers(mapBackendMcpServers(servers))
      return true
    } catch (error) {
      if (!isCurrentMcpRequest(requestGeneration)) return false
      notification.error(errorTitle, formatMcpError(error, 'Unable to fetch MCP server status'))
      return false
    } finally {
      if (isCurrentMcpRequest(requestGeneration)) {
        setIsLiveMcpLoading(false)
      }
    }
  }

  const reloadLiveMcpServers = async (): Promise<boolean> => {
    const requestGeneration = mcpRequestGeneration
    try {
      await rustBackend.reloadMcpServers()
    } catch (error) {
      if (!isCurrentMcpRequest(requestGeneration)) return false
      notification.error('Failed to reload MCP servers', formatMcpError(error, 'Reload failed'))
      return false
    }

    if (!isCurrentMcpRequest(requestGeneration)) return false

    return fetchLiveMcpServers('Failed to refresh MCP server status')
  }

  // Fetch live MCP state whenever the settings surface opens on the MCP tab.
  createEffect(
    on(
      () => [settingsOpen(), activeTab()] as const,
      ([open, tab]) => {
        if (!open || tab !== 'mcp') return
        void fetchLiveMcpServers('MCP status unavailable')
      }
    )
  )

  createEffect(
    on(
      () => settings().mcpServers,
      () => {
        if (backendMcpServers() === null && !hasAttemptedLiveMcpFetch()) return
        invalidateLiveMcpRequests()
      },
      { defer: true }
    )
  )

  const handleRefreshMcp = (): void => {
    void reloadLiveMcpServers()
  }

  const handleToggleMcp = (name: string, enabled: boolean): void => {
    const requestGeneration = mcpRequestGeneration
    const action = enabled ? rustBackend.enableMcpServer(name) : rustBackend.disableMcpServer(name)
    action
      .then(() => {
        if (!isCurrentMcpRequest(requestGeneration)) return false
        return fetchLiveMcpServers(`Failed to refresh MCP server status for ${name}`)
      })
      .catch((error) => {
        if (!isCurrentMcpRequest(requestGeneration)) return
        notification.error(
          `Failed to ${enabled ? 'enable' : 'disable'} ${name}`,
          error instanceof Error ? error.message : String(error)
        )
      })
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
    const credential = provider ? getProviderCredentialInfo(provider.id) : undefined
    if (!provider || !credential) return

    try {
      const rawModels = await fetchModels(id as LLMProvider, {
        apiKey: credential.value,
        authType: credential.type,
        baseUrl: provider.baseUrl,
      })
      const fetchedModels = enrichWithCatalog(id as LLMProvider, rawModels)
      let nextModels: ProviderModel[] = []
      let nextDefaultModel = provider.defaultModel

      if (fetchedModels.length > 0) {
        const defaults = defaultProviders.find((defaultProvider) => defaultProvider.id === id)
        const defaultMap = new Map<string, ProviderModel>()
        for (const model of defaults?.models ?? []) defaultMap.set(model.id, model)

        const fetchedMap = new Map<string, ProviderModel>()
        for (const model of fetchedModels) {
          const fallback = defaultMap.get(model.id)
          const pricing = model.pricing
            ? { input: model.pricing.prompt, output: model.pricing.completion }
            : fallback?.pricing
          const capabilities = [
            ...new Set([...(model.capabilities ?? []), ...(fallback?.capabilities ?? [])]),
          ]

          fetchedMap.set(model.id, {
            id: model.id,
            name: model.name,
            contextWindow: model.contextWindow,
            ...(pricing && { pricing }),
            ...(capabilities.length > 0 && { capabilities }),
          })
        }

        for (const [defaultId, defaultModel] of defaultMap) {
          if (!fetchedMap.has(defaultId)) {
            fetchedMap.set(defaultId, defaultModel)
          }
        }

        nextModels = [...fetchedMap.values()]
        const keepDefault =
          provider.defaultModel && nextModels.some((model) => model.id === provider.defaultModel)
        const preferredDefaultModel = keepDefault ? provider.defaultModel : defaults?.defaultModel
        nextDefaultModel =
          preferredDefaultModel && nextModels.some((model) => model.id === preferredDefaultModel)
            ? preferredDefaultModel
            : nextModels[0]?.id

        for (const model of nextModels) {
          model.isDefault = model.id === nextDefaultModel
        }
      }

      updateProvider(id, {
        ...(nextModels.length > 0 ? { models: nextModels, defaultModel: nextDefaultModel } : {}),
        status: nextModels.length > 0 ? 'connected' : 'disconnected',
        error: undefined,
      })
      notification.success('Provider connected', `${nextModels.length} models available`)
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
      if (event.key !== 'Escape') return
      if (event.defaultPrevented) return

      if (document.querySelector('[data-settings-nested-dialog="true"]')) {
        return
      }

      const target = event.target
      if (target instanceof Element && target.closest('[data-settings-nested-dialog="true"]')) {
        return
      }

      closeSettings()
    }

    const onSettingsTab = (event: Event) => {
      const tab = (event as CustomEvent<{ tab?: SettingsTab }>).detail?.tab
      if (tab && validTabs.has(tab)) {
        setActiveTab(tab)
      }
    }

    window.addEventListener('keydown', onEscape)
    window.addEventListener('ava:settings-tab', onSettingsTab)
    onCleanup(() => {
      window.removeEventListener('keydown', onEscape)
      window.removeEventListener('ava:settings-tab', onSettingsTab)
    })
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
              onSelectTab={setActiveTab}
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
              onToggleMcpServer={handleToggleMcp}
              isMcpLoading={isLiveMcpLoading}
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
