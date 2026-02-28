import {
  AlertTriangle,
  Code2,
  FolderSymlink,
  GitBranch,
  Globe,
  Puzzle,
  RefreshCw,
  Search,
  Shield,
  Trash2,
} from 'lucide-solid'
import { type Component, createMemo, createSignal, For, onCleanup, Show } from 'solid-js'
import { watchPluginDirectory } from '../../../services/extension-loader'
import { usePlugins } from '../../../stores/plugins'
import {
  PLUGIN_PERMISSION_META,
  type PluginPermission,
  type PluginScope,
  SENSITIVE_PERMISSIONS,
} from '../../../types/plugin'
import { PluginDetailPanel } from '../../plugins'

type DevModeStatus = 'idle' | 'watching' | 'reloading'

export const PluginsTab: Component = () => {
  const plugins = usePlugins()
  const [selectedPluginId, setSelectedPluginId] = createSignal<string | null>(null)
  const [devModePlugins, setDevModePlugins] = createSignal<Record<string, boolean>>({})
  const [devModeStatus, setDevModeStatus] = createSignal<Record<string, DevModeStatus>>({})
  const [devModeLogs, setDevModeLogs] = createSignal<Record<string, string[]>>({})
  const watchers = new Map<string, () => void>()

  // Git install dialog state
  const [showGitDialog, setShowGitDialog] = createSignal(false)
  const [gitUrl, setGitUrl] = createSignal('')
  const [gitInstalling, setGitInstalling] = createSignal(false)
  const [gitError, setGitError] = createSignal<string | null>(null)

  // Link local dialog state
  const [showLinkDialog, setShowLinkDialog] = createSignal(false)
  const [linkPath, setLinkPath] = createSignal('')
  const [linkInstalling, setLinkInstalling] = createSignal(false)
  const [linkError, setLinkError] = createSignal<string | null>(null)

  // Permission confirmation dialog state
  const [permConfirmPluginId, setPermConfirmPluginId] = createSignal<string | null>(null)
  const permConfirmPlugin = createMemo(() => {
    const id = permConfirmPluginId()
    if (!id) return null
    return plugins.plugins.find((p) => p.id === id) ?? null
  })

  /** Risk color for each permission level */
  const permissionColor = (perm: PluginPermission): string => {
    const risk = PLUGIN_PERMISSION_META[perm]?.risk ?? 'low'
    if (risk === 'high') return 'var(--error)'
    if (risk === 'medium') return 'var(--warning)'
    return 'var(--text-muted)'
  }

  /** Check if plugin has sensitive permissions and needs confirmation */
  const handleInstallWithPermCheck = (pluginId: string) => {
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

  const confirmInstallWithPerms = () => {
    const id = permConfirmPluginId()
    if (!id) return
    setPermConfirmPluginId(null)
    plugins.clearError(id)
    void plugins.install(id)
  }

  const appendLog = (pluginId: string, message: string) => {
    setDevModeLogs((prev) => {
      const current = prev[pluginId] ?? []
      const timestamp = new Date().toLocaleTimeString([], {
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
      })
      const next = [...current, `[${timestamp}] ${message}`].slice(-50)
      return { ...prev, [pluginId]: next }
    })
  }

  const toggleDevMode = (pluginId: string) => {
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

  const handleGitInstall = async () => {
    const url = gitUrl().trim()
    if (!url) return

    setGitInstalling(true)
    setGitError(null)

    try {
      await plugins.installFromGit(url)
      setGitUrl('')
      setShowGitDialog(false)
    } catch (err) {
      setGitError(err instanceof Error ? err.message : 'Failed to install from git.')
    } finally {
      setGitInstalling(false)
    }
  }

  const handleLinkLocal = async () => {
    const path = linkPath().trim()
    if (!path) return

    setLinkInstalling(true)
    setLinkError(null)

    try {
      await plugins.linkLocal(path)
      setLinkPath('')
      setShowLinkDialog(false)
    } catch (err) {
      setLinkError(err instanceof Error ? err.message : 'Failed to link local extension.')
    } finally {
      setLinkInstalling(false)
    }
  }

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

  const categoryLabel = (category: string) => category.charAt(0).toUpperCase() + category.slice(1)

  const formatSyncTime = (timestamp: number | null) => {
    if (!timestamp) return 'never'
    return new Date(timestamp).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })
  }

  const sourceLabel = (sourceType: string | undefined) => {
    if (sourceType === 'git') return 'Git'
    if (sourceType === 'local-link') return 'Local'
    return 'Catalog'
  }

  return (
    <div class="space-y-3">
      <div class="flex items-center justify-between">
        <div>
          <p class="text-xs text-[var(--text-secondary)]">Plugin manager (Settings-only)</p>
          <p class="text-[10px] text-[var(--text-muted)]">
            Installed: {plugins.installedCount()} / {plugins.plugins.length}
          </p>
        </div>
        <div class="flex items-center gap-1.5">
          <button
            type="button"
            onClick={() => setShowGitDialog(true)}
            class="flex items-center gap-1.5 px-2 py-1 text-[10px] text-[var(--text-secondary)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] hover:border-[var(--accent-muted)] transition-colors"
            title="Install from Git repository"
          >
            <GitBranch class="w-3 h-3" />
            Git
          </button>
          <button
            type="button"
            onClick={() => setShowLinkDialog(true)}
            class="flex items-center gap-1.5 px-2 py-1 text-[10px] text-[var(--text-secondary)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] hover:border-[var(--accent-muted)] transition-colors"
            title="Link local plugin directory"
          >
            <FolderSymlink class="w-3 h-3" />
            Link
          </button>
          <button
            type="button"
            onClick={() => {
              void plugins.refresh()
            }}
            class="flex items-center gap-1.5 px-2 py-1 text-[10px] text-[var(--text-secondary)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)]"
          >
            <RefreshCw class="w-3 h-3" />
            Refresh
          </button>
          <button
            type="button"
            onClick={() => {
              void plugins.syncCatalog()
            }}
            disabled={plugins.catalogStatus() === 'syncing'}
            class="flex items-center gap-1.5 px-2 py-1 text-[10px] text-[var(--text-secondary)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] disabled:opacity-50"
          >
            <RefreshCw
              class={`w-3 h-3 ${plugins.catalogStatus() === 'syncing' ? 'animate-spin' : ''}`}
            />
            Sync
          </button>
        </div>
      </div>

      {/* Git Install Dialog */}
      <Show when={showGitDialog()}>
        <div class="fixed inset-0 z-50 flex items-center justify-center bg-black/60">
          <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] p-6 max-w-md w-full shadow-2xl space-y-4">
            <div class="flex items-center gap-2">
              <GitBranch class="w-4 h-4 text-[var(--accent)]" />
              <h3 class="text-sm font-semibold text-[var(--text-primary)]">Install from Git</h3>
            </div>
            <p class="text-xs text-[var(--text-secondary)]">
              Enter a GitHub repository URL to install an extension.
            </p>
            <input
              type="text"
              placeholder="https://github.com/owner/repo"
              value={gitUrl()}
              onInput={(e) => setGitUrl(e.currentTarget.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter') void handleGitInstall()
                if (e.key === 'Escape') setShowGitDialog(false)
              }}
              class="w-full px-3 py-2 text-sm rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
              autofocus
            />
            <Show when={gitError()}>
              <p class="text-[10px] text-[var(--error)]">{gitError()}</p>
            </Show>
            <div class="flex gap-2 justify-end">
              <button
                type="button"
                onClick={() => setShowGitDialog(false)}
                class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
              >
                Cancel
              </button>
              <button
                type="button"
                onClick={() => void handleGitInstall()}
                disabled={!gitUrl().trim() || gitInstalling()}
                class="px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:brightness-110 transition-colors disabled:opacity-50"
              >
                {gitInstalling() ? 'Installing...' : 'Install'}
              </button>
            </div>
          </div>
        </div>
      </Show>

      {/* Link Local Dialog */}
      <Show when={showLinkDialog()}>
        <div class="fixed inset-0 z-50 flex items-center justify-center bg-black/60">
          <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] p-6 max-w-md w-full shadow-2xl space-y-4">
            <div class="flex items-center gap-2">
              <FolderSymlink class="w-4 h-4 text-[var(--accent)]" />
              <h3 class="text-sm font-semibold text-[var(--text-primary)]">Link Local Extension</h3>
            </div>
            <p class="text-xs text-[var(--text-secondary)]">
              Enter the absolute path to your local plugin directory. A symlink will be created.
            </p>
            <input
              type="text"
              placeholder="/path/to/my-plugin"
              value={linkPath()}
              onInput={(e) => setLinkPath(e.currentTarget.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter') void handleLinkLocal()
                if (e.key === 'Escape') setShowLinkDialog(false)
              }}
              class="w-full px-3 py-2 text-sm rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
              autofocus
            />
            <Show when={linkError()}>
              <p class="text-[10px] text-[var(--error)]">{linkError()}</p>
            </Show>
            <div class="flex gap-2 justify-end">
              <button
                type="button"
                onClick={() => setShowLinkDialog(false)}
                class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
              >
                Cancel
              </button>
              <button
                type="button"
                onClick={() => void handleLinkLocal()}
                disabled={!linkPath().trim() || linkInstalling()}
                class="px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:brightness-110 transition-colors disabled:opacity-50"
              >
                {linkInstalling() ? 'Linking...' : 'Link'}
              </button>
            </div>
          </div>
        </div>
      </Show>

      {/* Permission Confirmation Dialog */}
      <Show when={permConfirmPlugin()}>
        {(plugin) => (
          <div class="fixed inset-0 z-50 flex items-center justify-center bg-black/60">
            <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] p-6 max-w-md w-full shadow-2xl space-y-4">
              <div class="flex items-center gap-2">
                <AlertTriangle class="w-4 h-4 text-[var(--warning)]" />
                <h3 class="text-sm font-semibold text-[var(--text-primary)]">
                  Sensitive Permissions Required
                </h3>
              </div>
              <p class="text-xs text-[var(--text-secondary)]">
                <strong>{plugin().name}</strong> requests the following permissions:
              </p>
              <div class="flex flex-wrap gap-1.5">
                <For each={plugin().permissions ?? []}>
                  {(perm) => {
                    const meta = PLUGIN_PERMISSION_META[perm as PluginPermission]
                    return (
                      <span
                        class="inline-flex items-center gap-1 px-2 py-1 text-[10px] font-medium rounded-full border"
                        style={{
                          color: permissionColor(perm as PluginPermission),
                          'border-color': permissionColor(perm as PluginPermission),
                          'background-color': `color-mix(in srgb, ${permissionColor(perm as PluginPermission)} 10%, transparent)`,
                        }}
                      >
                        <Shield class="w-2.5 h-2.5" />
                        {meta?.label ?? perm}
                      </span>
                    )
                  }}
                </For>
              </div>
              <p class="text-[10px] text-[var(--text-muted)]">
                This plugin can access sensitive system resources. Only install plugins from sources
                you trust.
              </p>
              <div class="flex gap-2 justify-end">
                <button
                  type="button"
                  onClick={() => setPermConfirmPluginId(null)}
                  class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
                >
                  Cancel
                </button>
                <button
                  type="button"
                  onClick={confirmInstallWithPerms}
                  class="px-3 py-1.5 text-xs font-medium bg-[var(--warning)] text-white rounded-[var(--radius-md)] hover:brightness-110 transition-colors"
                >
                  Install Anyway
                </button>
              </div>
            </div>
          </div>
        )}
      </Show>

      <div class="text-[10px] text-[var(--text-muted)]">
        <span>Status: {plugins.catalogStatus()}</span>
        <span class="mx-1">&bull;</span>
        <span>Last sync: {formatSyncTime(plugins.lastCatalogSyncAt())}</span>
      </div>

      <Show when={plugins.catalogError()}>
        <p class="text-[10px] text-[var(--error)]">{plugins.catalogError()}</p>
      </Show>

      <div class="flex items-center gap-2">
        <div class="relative flex-1">
          <Search class="w-3.5 h-3.5 absolute left-2 top-1/2 -translate-y-1/2 text-[var(--text-muted)]" />
          <input
            value={plugins.search()}
            onInput={(e) => plugins.setSearch(e.currentTarget.value)}
            placeholder="Search plugins..."
            class="w-full pl-7 pr-2 py-1.5 text-[11px] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] text-[var(--text-secondary)]"
          />
        </div>
        <button
          type="button"
          onClick={() => plugins.setShowInstalledOnly(!plugins.showInstalledOnly())}
          class={`px-2 py-1.5 text-[10px] rounded-[var(--radius-md)] border ${plugins.showInstalledOnly() ? 'text-[var(--accent)] border-[var(--accent-muted)] bg-[var(--accent-subtle)]' : 'text-[var(--text-secondary)] border-[var(--border-subtle)] bg-[var(--surface-raised)]'}`}
        >
          Installed only
        </button>
      </div>

      <div class="flex items-center gap-1.5 flex-wrap">
        <For each={plugins.categories()}>
          {(category) => (
            <button
              type="button"
              onClick={() => plugins.setCategoryFilter(category)}
              class={`px-2 py-1 text-[10px] rounded-[var(--radius-md)] border ${plugins.categoryFilter() === category ? 'text-[var(--accent)] border-[var(--accent-muted)] bg-[var(--accent-subtle)]' : 'text-[var(--text-secondary)] border-[var(--border-subtle)] bg-[var(--surface-raised)]'}`}
            >
              {category === 'all' ? 'All' : categoryLabel(category)}
            </button>
          )}
        </For>
      </div>

      <Show when={showFeatured()}>
        <div class="space-y-1.5">
          <p class="text-[10px] uppercase tracking-wide text-[var(--text-muted)]">Featured</p>
          <div class="grid grid-cols-1 sm:grid-cols-2 gap-1.5">
            <For each={plugins.featuredPlugins()}>
              {(plugin) => (
                <button
                  type="button"
                  onClick={() => setSelectedPluginId(plugin.id)}
                  class="text-left px-2.5 py-2 rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] hover:border-[var(--accent-muted)] transition-colors"
                >
                  <div class="flex items-center gap-1.5 mb-0.5">
                    <Puzzle class="w-3 h-3 text-[var(--accent)]" />
                    <span class="text-[11px] text-[var(--text-primary)]">{plugin.name}</span>
                    <span class="text-[9px] text-[var(--text-muted)]">v{plugin.version}</span>
                  </div>
                  <div class="mb-0.5 flex items-center gap-1 text-[9px] text-[var(--text-muted)]">
                    <span class="uppercase">{plugin.source}</span>
                    <span>&bull;</span>
                    <span
                      class={
                        plugin.trust === 'verified'
                          ? 'text-[var(--success)]'
                          : 'text-[var(--accent)]'
                      }
                    >
                      {plugin.trust}
                    </span>
                  </div>
                  <p class="text-[10px] text-[var(--text-muted)] line-clamp-2">
                    {plugin.description}
                  </p>
                </button>
              )}
            </For>
          </div>
        </div>
      </Show>

      <div class="space-y-1.5">
        <Show
          when={plugins.filteredPlugins().length > 0}
          fallback={<p class="text-[11px] text-[var(--text-muted)]">{emptyStateMessage()}</p>}
        >
          <For each={plugins.filteredPlugins()}>
            {(plugin) => {
              const state = () =>
                plugins.pluginState()[plugin.id] ?? {
                  installed: false,
                  enabled: false,
                }
              const pending = () => plugins.pendingAction(plugin.id)
              const isBusy = () => pending() !== null
              const error = () => plugins.errorFor(plugin.id)
              return (
                <div
                  class={`w-full flex items-center gap-2.5 px-2.5 py-2 rounded-[var(--radius-md)] border ${selectedPluginId() === plugin.id ? 'border-[var(--accent-muted)] bg-[var(--accent-subtle)]' : 'border-[var(--border-subtle)] bg-[var(--surface)]'}`}
                >
                  <div class="flex-1 min-w-0">
                    <button
                      type="button"
                      onClick={() => setSelectedPluginId(plugin.id)}
                      class="w-full flex items-center gap-2.5 min-w-0 text-left"
                    >
                      <Puzzle class="w-3.5 h-3.5 flex-shrink-0 text-[var(--text-muted)]" />
                      <div class="flex-1 min-w-0">
                        <p class="text-xs text-[var(--text-primary)]">{plugin.name}</p>
                        <div class="flex items-center gap-1 text-[9px] text-[var(--text-muted)]">
                          <span>v{plugin.version}</span>
                          <span>&bull;</span>
                          <span class="uppercase">{plugin.source}</span>
                          <Show when={state().sourceType && state().sourceType !== 'catalog'}>
                            <span>&bull;</span>
                            <span
                              class={
                                state().sourceType === 'git'
                                  ? 'text-[var(--accent)]'
                                  : 'text-[var(--warning)]'
                              }
                            >
                              {sourceLabel(state().sourceType)}
                            </span>
                          </Show>
                          <span>&bull;</span>
                          <span
                            class={
                              plugin.trust === 'verified'
                                ? 'text-[var(--success)]'
                                : 'text-[var(--accent)]'
                            }
                          >
                            {plugin.trust}
                          </span>
                        </div>
                        <p class="text-[10px] text-[var(--text-muted)]">{plugin.description}</p>
                        <Show when={state().sourceUrl}>
                          <p class="text-[9px] text-[var(--text-muted)] truncate">
                            {state().sourceUrl}
                          </p>
                        </Show>
                      </div>
                    </button>
                    <Show when={error()}>
                      <div class="mt-0.5 flex items-center gap-2">
                        <p class="text-[10px] text-[var(--error)]">{error()}</p>
                        <button
                          type="button"
                          onClick={() => {
                            void plugins.retry(plugin.id)
                          }}
                          disabled={isBusy() || plugins.failedAction(plugin.id) === null}
                          class="px-1.5 py-0.5 text-[9px] rounded-[var(--radius-sm)] border border-[var(--border-default)] text-[var(--text-secondary)] disabled:opacity-50"
                        >
                          Retry
                        </button>
                        <button
                          type="button"
                          onClick={() => {
                            void plugins.recover(plugin.id)
                          }}
                          disabled={isBusy()}
                          class="px-1.5 py-0.5 text-[9px] rounded-[var(--radius-sm)] border border-[var(--error)] text-[var(--error)] disabled:opacity-50"
                        >
                          Recover
                        </button>
                      </div>
                    </Show>
                  </div>
                  <Show
                    when={state().installed}
                    fallback={
                      <button
                        type="button"
                        onClick={() => handleInstallWithPermCheck(plugin.id)}
                        disabled={isBusy()}
                        class="px-2 py-1 text-[10px] text-white bg-[var(--accent)] rounded-[var(--radius-md)] disabled:opacity-60"
                      >
                        {pending() === 'install' ? 'Installing...' : 'Install'}
                      </button>
                    }
                  >
                    {/* Scope toggle */}
                    <button
                      type="button"
                      onClick={(e) => {
                        e.stopPropagation()
                        const current = state().scope || 'global'
                        const next: PluginScope = current === 'global' ? 'project' : 'global'
                        plugins.setPluginScope(plugin.id, next)
                      }}
                      class="p-1 text-[var(--text-muted)] hover:text-[var(--text-secondary)] rounded-[var(--radius-sm)] transition-colors"
                      title={
                        (state().scope || 'global') === 'global'
                          ? 'Global scope (click for project)'
                          : 'Project scope (click for global)'
                      }
                    >
                      <Globe class="w-3 h-3" />
                    </button>
                    <button
                      type="button"
                      onClick={(e) => {
                        e.stopPropagation()
                        plugins.clearError(plugin.id)
                        void plugins.toggleEnabled(plugin.id)
                      }}
                      disabled={isBusy()}
                      class={`px-2 py-1 text-[10px] rounded-[var(--radius-md)] border disabled:opacity-60 ${state().enabled ? 'text-[var(--success)] border-[var(--success)]' : 'text-[var(--text-muted)] border-[var(--border-default)]'}`}
                    >
                      {pending() === 'toggle'
                        ? 'Updating...'
                        : state().enabled
                          ? 'Enabled'
                          : 'Disabled'}
                    </button>
                    <button
                      type="button"
                      onClick={(e) => {
                        e.stopPropagation()
                        plugins.clearError(plugin.id)
                        void plugins.uninstall(plugin.id)
                      }}
                      disabled={isBusy()}
                      class="p-1.5 text-[var(--error)] hover:bg-[var(--error-subtle)] rounded-[var(--radius-sm)] disabled:opacity-60"
                      aria-label="Uninstall plugin"
                    >
                      <Show
                        when={pending() === 'uninstall'}
                        fallback={<Trash2 class="w-3.5 h-3.5" />}
                      >
                        <RefreshCw class="w-3.5 h-3.5 animate-spin" />
                      </Show>
                    </button>
                  </Show>
                </div>
              )
            }}
          </For>
        </Show>
      </div>

      <PluginDetailPanel plugin={selectedPlugin()} state={selectedState()} />

      {/* Permission badges for selected plugin */}
      <Show when={selectedPlugin()?.permissions && selectedPlugin()!.permissions!.length > 0}>
        <div class="border border-[var(--border-subtle)] rounded-[var(--radius-md)] bg-[var(--surface)] p-3 space-y-1.5">
          <div class="flex items-center gap-2">
            <Shield class="w-3.5 h-3.5 text-[var(--text-muted)]" />
            <span class="text-[11px] text-[var(--text-primary)]">Permissions</span>
          </div>
          <div class="flex flex-wrap gap-1.5">
            <For each={selectedPlugin()!.permissions!}>
              {(perm) => {
                const meta = PLUGIN_PERMISSION_META[perm]
                return (
                  <span
                    class="inline-flex items-center gap-1 px-2 py-0.5 text-[10px] font-medium rounded-full border"
                    style={{
                      color: permissionColor(perm),
                      'border-color': permissionColor(perm),
                      'background-color': `color-mix(in srgb, ${permissionColor(perm)} 10%, transparent)`,
                    }}
                    title={meta?.description ?? ''}
                  >
                    <Shield class="w-2.5 h-2.5" />
                    {meta?.label ?? perm}
                  </span>
                )
              }}
            </For>
          </div>
          <Show
            when={selectedPlugin()!.permissions!.some((p) => SENSITIVE_PERMISSIONS.includes(p))}
          >
            <p class="text-[9px] text-[var(--warning)] flex items-center gap-1">
              <AlertTriangle class="w-2.5 h-2.5" />
              This plugin requests sensitive permissions
            </p>
          </Show>
        </div>
      </Show>

      {/* Detail panel: git source info for selected plugin */}
      <Show
        when={
          selectedPlugin() &&
          selectedState()?.sourceType &&
          selectedState()?.sourceType !== 'catalog'
        }
      >
        <div class="border border-[var(--border-subtle)] rounded-[var(--radius-md)] bg-[var(--surface)] p-3 space-y-1.5">
          <div class="flex items-center gap-2">
            <Show
              when={selectedState()?.sourceType === 'git'}
              fallback={<FolderSymlink class="w-3.5 h-3.5 text-[var(--warning)]" />}
            >
              <GitBranch class="w-3.5 h-3.5 text-[var(--accent)]" />
            </Show>
            <span class="text-[11px] text-[var(--text-primary)]">
              {selectedState()?.sourceType === 'git' ? 'Git Source' : 'Local Link'}
            </span>
          </div>
          <Show when={selectedState()?.sourceUrl}>
            <p class="text-[10px] text-[var(--text-secondary)] break-all">
              {selectedState()?.sourceUrl}
            </p>
          </Show>
          <Show when={selectedState()?.version}>
            <p class="text-[10px] text-[var(--text-muted)]">Version: {selectedState()?.version}</p>
          </Show>
          <p class="text-[10px] text-[var(--text-muted)]">
            Scope: {selectedState()?.scope || 'global'}
          </p>
        </div>
      </Show>

      <Show when={selectedPlugin() && selectedState()?.installed}>
        {(_) => {
          const pluginId = () => selectedPluginId()!
          const isDevMode = () => devModePlugins()[pluginId()] ?? false
          const status = () => devModeStatus()[pluginId()] ?? 'idle'
          const logs = () => devModeLogs()[pluginId()] ?? []
          return (
            <div class="border border-[var(--border-subtle)] rounded-[var(--radius-md)] bg-[var(--surface)] p-3 space-y-2">
              <div class="flex items-center justify-between">
                <div class="flex items-center gap-2">
                  <Code2 class="w-3.5 h-3.5 text-[var(--text-muted)]" />
                  <span class="text-[11px] text-[var(--text-primary)]">Dev Mode</span>
                  <Show when={isDevMode()}>
                    <span
                      class={`px-1.5 py-0.5 text-[9px] rounded-full ${
                        status() === 'reloading'
                          ? 'bg-[var(--warning-subtle)] text-[var(--warning)]'
                          : 'bg-[var(--success-subtle)] text-[var(--success)]'
                      }`}
                    >
                      {status() === 'reloading' ? 'Reloading...' : 'Watching...'}
                    </span>
                  </Show>
                </div>
                <button
                  type="button"
                  onClick={() => toggleDevMode(pluginId())}
                  class={`relative w-8 h-[18px] rounded-full transition-colors flex-shrink-0 ${
                    isDevMode() ? 'bg-[var(--accent)]' : 'bg-[var(--alpha-white-10)]'
                  }`}
                  aria-label={`${isDevMode() ? 'Disable' : 'Enable'} dev mode`}
                >
                  <span
                    class={`absolute top-[2px] w-[14px] h-[14px] rounded-full bg-white shadow-sm transition-transform ${
                      isDevMode() ? 'translate-x-[16px]' : 'translate-x-[2px]'
                    }`}
                  />
                </button>
              </div>
              <p class="text-[10px] text-[var(--text-muted)]">
                Watches plugin files and auto-reloads on change.
              </p>
              <Show when={logs().length > 0}>
                <div class="bg-[var(--gray-1)] rounded-[var(--radius-sm)] p-2 max-h-24 overflow-y-auto">
                  <pre class="text-[9px] text-[var(--text-muted)] font-mono whitespace-pre-wrap">
                    {logs().join('\n')}
                  </pre>
                </div>
              </Show>
            </div>
          )
        }}
      </Show>
    </div>
  )
}
