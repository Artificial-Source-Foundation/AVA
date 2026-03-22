/**
 * Plugins Tab
 *
 * Thin orchestrator that wires together PluginSearch, PluginCard,
 * PluginInstallDialog, PluginDevMode, and the shared PluginDetailPanel.
 */

import { Code2, Info, Package } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, onCleanup, Show } from 'solid-js'
import { watchPluginDirectory } from '../../../services/extension-loader'
import { usePlugins } from '../../../stores/plugins'
import { type PluginSortBy, sortPlugins } from '../../../stores/plugins-catalog'
import { type PluginPermission, SENSITIVE_PERMISSIONS } from '../../../types/plugin'
import { PluginDetailPanel } from '../../plugins'
import { PluginWizard } from '../../plugins/PluginWizard'
import { PublishDialog } from '../../plugins/PublishDialog'
import { SettingsCard } from '../SettingsCard'
import { SETTINGS_CARD_GAP } from '../settings-constants'
import {
  type DevModeStatus,
  FeaturedPluginCard,
  formatSyncTime,
  GitInstallDialog,
  LinkLocalDialog,
  PermissionConfirmDialog,
  PluginCard,
  PluginDevMode,
  PluginPermissionBadges,
  PluginSearch,
  PluginSourceInfo,
  PluginToolbar,
} from './plugins-tab'

export const PluginsTab: Component = () => {
  const plugins = usePlugins()
  const [selectedPluginId, setSelectedPluginId] = createSignal<string | null>(null)
  const [devModePlugins, setDevModePlugins] = createSignal<Record<string, boolean>>({})
  const [devModeStatus, setDevModeStatus] = createSignal<Record<string, DevModeStatus>>({})
  const [devModeLogs, setDevModeLogs] = createSignal<Record<string, string[]>>({})
  const watchers = new Map<string, () => void>()

  // Dialog visibility
  const [showGitDialog, setShowGitDialog] = createSignal(false)
  const [showLinkDialog, setShowLinkDialog] = createSignal(false)
  const [showPublish, setShowPublish] = createSignal(false)
  const [showWizard, setShowWizard] = createSignal(false)
  const [sortBy, setSortBy] = createSignal<PluginSortBy>('popular')

  // Permission confirmation
  const [permConfirmPluginId, setPermConfirmPluginId] = createSignal<string | null>(null)
  const permConfirmPlugin = createMemo(() => {
    const id = permConfirmPluginId()
    if (!id) return null
    return plugins.plugins.find((p) => p.id === id) ?? null
  })

  const handleInstallWithPermCheck = (pluginId: string): void => {
    const plugin = plugins.plugins.find((p) => p.id === pluginId)
    const perms = plugin?.permissions ?? []
    const hasSensitive = perms.some((p) => SENSITIVE_PERMISSIONS.includes(p as PluginPermission))
    if (hasSensitive) {
      setPermConfirmPluginId(pluginId)
    } else {
      plugins.clearError(pluginId)
      void plugins.install(pluginId)
    }
  }

  const confirmInstallWithPerms = (): void => {
    const id = permConfirmPluginId()
    if (!id) return
    setPermConfirmPluginId(null)
    plugins.clearError(id)
    void plugins.install(id)
  }

  // Dev-mode helpers
  const appendLog = (pluginId: string, message: string): void => {
    setDevModeLogs((prev) => {
      const current = prev[pluginId] ?? []
      const timestamp = new Date().toLocaleTimeString([], {
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
      })
      return { ...prev, [pluginId]: [...current, `[${timestamp}] ${message}`].slice(-50) }
    })
  }

  const toggleDevMode = (pluginId: string): void => {
    const isOn = devModePlugins()[pluginId]
    if (isOn) {
      const cleanup = watchers.get(pluginId)
      if (cleanup) {
        cleanup()
        watchers.delete(pluginId)
      }
      setDevModePlugins((prev) => ({ ...prev, [pluginId]: false }))
      setDevModeStatus((prev) => ({ ...prev, [pluginId]: 'idle' }))
      appendLog(pluginId, 'Dev mode disabled')
    } else {
      const state = plugins.pluginState()[pluginId]
      if (!state?.installPath) {
        appendLog(pluginId, 'No install path — cannot watch')
        return
      }

      setDevModePlugins((prev) => ({ ...prev, [pluginId]: true }))
      setDevModeStatus((prev) => ({ ...prev, [pluginId]: 'watching' }))
      appendLog(pluginId, `Watching ${state.installPath}`)

      const cleanup = watchPluginDirectory(state.installPath, async () => {
        setDevModeStatus((prev) => ({ ...prev, [pluginId]: 'reloading' }))
        appendLog(pluginId, 'File change detected, reloading...')
        try {
          await plugins.refresh()
          appendLog(pluginId, 'Reload complete')
        } catch (err) {
          appendLog(
            pluginId,
            `Reload failed: ${err instanceof Error ? err.message : 'Unknown error'}`
          )
        } finally {
          setDevModeStatus((prev) => ({ ...prev, [pluginId]: 'watching' }))
        }
      })
      watchers.set(pluginId, cleanup)
    }
  }

  onCleanup(() => {
    for (const cleanup of watchers.values()) cleanup()
    watchers.clear()
  })

  // Derived state
  const selectedPlugin = createMemo(() => {
    const id = selectedPluginId()
    if (!id) return null
    return plugins.plugins.find((plugin) => plugin.id === id) ?? null
  })

  const selectedState = createMemo(() => {
    const id = selectedPluginId()
    if (!id) return null
    return plugins.pluginState()[id] ?? { installed: false, enabled: false }
  })

  const showFeatured = createMemo(
    () =>
      !plugins.search().trim() && plugins.categoryFilter() === 'all' && !plugins.showInstalledOnly()
  )

  const emptyStateMessage = createMemo(() => {
    if (plugins.showInstalledOnly()) return 'No installed plugins match this filter yet.'
    if (plugins.search().trim()) return `No plugins found for "${plugins.search().trim()}".`
    if (plugins.categoryFilter() !== 'all') return 'No plugins in this category.'
    return 'No plugins match your filters.'
  })

  const sortedPlugins = createMemo(() => sortPlugins(plugins.filteredPlugins(), sortBy()))

  return (
    <div class="grid grid-cols-1" style={{ gap: SETTINGS_CARD_GAP }}>
      {/* Plugins card */}
      <SettingsCard
        icon={Package}
        title="Plugins"
        description="Extend AVA with community and custom plugins"
      >
        {/* Header toolbar */}
        <PluginToolbar
          onShowWizard={() => setShowWizard(true)}
          onShowPublish={() => setShowPublish(true)}
          onShowGitDialog={() => setShowGitDialog(true)}
          onShowLinkDialog={() => setShowLinkDialog(true)}
        />

        {/* Status bar */}
        <div class="text-[var(--settings-text-description)] text-[var(--text-muted)]">
          <span>Status: {plugins.catalogStatus()}</span>
          <span class="mx-1">&bull;</span>
          <span>Last sync: {formatSyncTime(plugins.lastCatalogSyncAt())}</span>
        </div>

        <Show when={plugins.catalogError()}>
          <p class="text-[var(--settings-text-badge)] text-[var(--error)]">
            {plugins.catalogError()}
          </p>
        </Show>

        {/* Search, category filter, sort */}
        <PluginSearch sortBy={sortBy} onSortChange={setSortBy} />

        {/* Featured section */}
        <Show when={showFeatured()}>
          <div class="space-y-1.5">
            <p class="text-[var(--settings-text-badge)] uppercase tracking-wide text-[var(--text-muted)]">
              Featured
            </p>
            <div class="grid grid-cols-1 sm:grid-cols-2 gap-1.5">
              <For each={plugins.featuredPlugins()}>
                {(plugin) => <FeaturedPluginCard plugin={plugin} onSelect={setSelectedPluginId} />}
              </For>
            </div>
          </div>
        </Show>

        {/* Plugin list */}
        <div class="space-y-1.5">
          <Show
            when={sortedPlugins().length > 0}
            fallback={
              <div class="flex flex-col items-center justify-center py-10 text-center">
                <Package class="w-8 h-8 text-[var(--text-muted)] mb-2" />
                <p class="text-[var(--settings-text-description)] text-[var(--text-secondary)] mb-1">
                  No plugins installed
                </p>
                <p class="text-[var(--settings-text-description)] text-[var(--text-muted)] max-w-xs mb-3">
                  Plugins add capabilities to AVA. {emptyStateMessage()}
                </p>
                <button
                  type="button"
                  onClick={() => setShowWizard(true)}
                  class="inline-flex items-center gap-1.5 px-3 py-1.5 text-[var(--settings-text-button)] font-medium rounded-[var(--radius-md)] bg-[var(--accent)] text-white hover:brightness-110 transition-colors"
                >
                  Create your first plugin
                </button>
              </div>
            }
          >
            <For each={sortedPlugins()}>
              {(plugin) => (
                <PluginCard
                  plugin={plugin}
                  isSelected={selectedPluginId() === plugin.id}
                  onSelect={setSelectedPluginId}
                  onInstall={handleInstallWithPermCheck}
                />
              )}
            </For>
          </Show>
        </div>
      </SettingsCard>

      {/* Plugin Details card */}
      <Show when={selectedPlugin()}>
        <SettingsCard
          icon={Info}
          title="Plugin Details"
          description="Configuration and info for the selected plugin"
        >
          <PluginDetailPanel plugin={selectedPlugin()} state={selectedState()} />
          <PluginPermissionBadges plugin={selectedPlugin} />
          <PluginSourceInfo plugin={selectedPlugin} state={selectedState} />
        </SettingsCard>
      </Show>

      {/* Developer Mode card */}
      <Show when={selectedPlugin() && selectedState()?.installed}>
        {(_) => {
          const pluginId = () => selectedPluginId()!
          return (
            <SettingsCard
              icon={Code2}
              title="Developer Mode"
              description="Live reload and debug tools for plugin development"
            >
              <PluginDevMode
                pluginId={pluginId}
                isDevMode={() => devModePlugins()[pluginId()] ?? false}
                status={() => devModeStatus()[pluginId()] ?? 'idle'}
                logs={() => devModeLogs()[pluginId()] ?? []}
                onToggle={toggleDevMode}
              />
            </SettingsCard>
          )
        }}
      </Show>

      {/* Dialogs (outside cards — they're modals) */}
      <GitInstallDialog open={showGitDialog} onClose={() => setShowGitDialog(false)} />
      <LinkLocalDialog open={showLinkDialog} onClose={() => setShowLinkDialog(false)} />
      <PermissionConfirmDialog
        plugin={permConfirmPlugin}
        onCancel={() => setPermConfirmPluginId(null)}
        onConfirm={confirmInstallWithPerms}
      />
      <PublishDialog open={showPublish()} onClose={() => setShowPublish(false)} />
      <PluginWizard open={showWizard()} onClose={() => setShowWizard(false)} />
    </div>
  )
}
