import { createMemo, createSignal } from 'solid-js'
import { loadPluginsState } from '../services/plugins-fs'
import type { PluginCatalogItem, PluginScope, PluginState } from '../types/plugin'
import {
  FEATURED_PLUGIN_IDS,
  PLUGIN_CATALOG,
  type PluginCatalogStatus,
  syncPluginCatalog,
} from './plugins-catalog'
import {
  install,
  installFromGit,
  linkLocal,
  type PluginAction,
  type PluginsStoreInternals,
  recover,
  retry,
  toggleEnabled,
  uninstall,
  uninstallGit,
} from './plugins-lifecycle'

type PluginCategoryFilter = PluginCatalogItem['category'] | 'all'

let store: ReturnType<typeof createPluginsStore> | null = null

export function resetPluginsStore(): void {
  store = null
}

function createPluginsStore() {
  // ========================================================================
  // Signals
  // ========================================================================

  const [search, setSearch] = createSignal('')
  const [showInstalledOnly, setShowInstalledOnly] = createSignal(false)
  const [categoryFilter, setCategoryFilter] = createSignal<PluginCategoryFilter>('all')
  const [pluginState, setPluginState] = createSignal<Record<string, PluginState>>({})
  const [catalogStatus, setCatalogStatus] = createSignal<PluginCatalogStatus>('idle')
  const [catalogError, setCatalogError] = createSignal<string | null>(null)
  const [lastCatalogSyncAt, setLastCatalogSyncAt] = createSignal<number | null>(null)
  const [pendingActions, setPendingActions] = createSignal<Record<string, PluginAction | null>>({})
  const [errorsByPlugin, setErrorsByPlugin] = createSignal<Record<string, string>>({})
  const [failedActionsByPlugin, setFailedActionsByPlugin] = createSignal<
    Record<string, PluginAction | null>
  >({})
  let lifecycleQueue: Promise<void> = Promise.resolve()

  // ========================================================================
  // Internal helpers (shared with lifecycle module via PluginsStoreInternals)
  // ========================================================================

  const setState = (id: string, next: PluginState): void => {
    setPluginState((prev) => ({ ...prev, [id]: next }))
  }

  const clearState = (id: string): void => {
    const current = pluginState()
    if (!(id in current)) return
    const next = { ...current }
    delete next[id]
    setPluginState(next)
  }

  const setPendingAction = (id: string, action: PluginAction | null): void => {
    setPendingActions((prev) => ({ ...prev, [id]: action }))
  }

  const setError = (id: string, message: string): void => {
    setErrorsByPlugin((prev) => ({ ...prev, [id]: message }))
  }

  const clearError = (id: string): void => {
    setErrorsByPlugin((prev) => {
      if (!(id in prev)) return prev
      const next = { ...prev }
      delete next[id]
      return next
    })
  }

  const setFailedAction = (id: string, action: PluginAction | null): void => {
    setFailedActionsByPlugin((prev) => ({ ...prev, [id]: action }))
  }

  const runLifecycle = <T>(operation: () => Promise<T>): Promise<T> => {
    const task = lifecycleQueue.then(operation, operation)
    lifecycleQueue = task.then(
      () => undefined,
      () => undefined
    )
    return task
  }

  const internals: PluginsStoreInternals = {
    pluginState,
    pendingActions,
    failedActionsByPlugin,
    setState,
    clearState,
    setPendingAction,
    setError,
    clearError,
    setFailedAction,
    runLifecycle,
  }

  // ========================================================================
  // Computed
  // ========================================================================

  const categories = createMemo(() => {
    const unique = new Set<PluginCatalogItem['category']>()
    for (const plugin of PLUGIN_CATALOG) unique.add(plugin.category)
    return ['all', ...Array.from(unique)] as PluginCategoryFilter[]
  })

  const featuredPlugins = createMemo(() =>
    PLUGIN_CATALOG.filter((plugin) => FEATURED_PLUGIN_IDS.includes(plugin.id))
  )

  const filteredPlugins = createMemo(() => {
    const query = search().trim().toLowerCase()
    const category = categoryFilter()
    return PLUGIN_CATALOG.filter((plugin) => {
      const state = pluginState()[plugin.id]
      if (showInstalledOnly() && !state?.installed) return false
      if (category !== 'all' && plugin.category !== category) return false
      if (!query) return true
      return (
        plugin.name.toLowerCase().includes(query) ||
        plugin.description.toLowerCase().includes(query) ||
        plugin.category.toLowerCase().includes(query)
      )
    })
  })

  const installedCount = createMemo(
    () => Object.values(pluginState()).filter((entry) => entry.installed).length
  )

  const setPluginScope = (id: string, scope: PluginScope): void => {
    const current = pluginState()[id]
    if (!current?.installed) return
    setState(id, { ...current, scope })
  }

  const getPluginVersion = (id: string): string | undefined => {
    return pluginState()[id]?.version
  }

  const hasUpdate = (id: string): boolean => {
    const installed = pluginState()[id]
    if (!installed?.installed || !installed.version) return false
    const catalogItem = PLUGIN_CATALOG.find((p) => p.id === id)
    if (!catalogItem?.version) return false
    return catalogItem.version !== installed.version
  }

  const pluginsWithUpdates = createMemo(() => PLUGIN_CATALOG.filter((p) => hasUpdate(p.id)))

  // ========================================================================
  // Catalog sync
  // ========================================================================

  const syncCatalog = async (): Promise<void> => {
    setCatalogStatus('syncing')
    setCatalogError(null)
    try {
      await syncPluginCatalog()
      setCatalogStatus('ready')
      setLastCatalogSyncAt(Date.now())
    } catch (error) {
      setCatalogStatus('error')
      setCatalogError(
        error instanceof Error ? error.message : 'Failed to sync plugin catalog metadata.'
      )
    }
  }

  const refreshCatalog = async (): Promise<void> => {
    setCatalogStatus('syncing')
    setCatalogError(null)
    try {
      await syncPluginCatalog(true)
      setCatalogStatus('ready')
      setLastCatalogSyncAt(Date.now())
    } catch (error) {
      setCatalogStatus('error')
      setCatalogError(error instanceof Error ? error.message : 'Failed to refresh plugin catalog.')
    }
  }

  const refresh = async (): Promise<void> => {
    const state = await loadPluginsState()
    setPluginState(state)
  }

  void refresh()

  // ========================================================================
  // Public API
  // ========================================================================

  return {
    plugins: PLUGIN_CATALOG,
    filteredPlugins,
    search,
    showInstalledOnly,
    categoryFilter,
    pluginState,
    catalogStatus,
    catalogError,
    lastCatalogSyncAt,
    installedCount,
    categories,
    featuredPlugins,
    pluginsWithUpdates,
    setSearch,
    setShowInstalledOnly,
    setCategoryFilter,
    syncCatalog,
    refreshCatalog,
    install: (id: string) => install(internals, id),
    uninstall: (id: string) => uninstall(internals, id),
    toggleEnabled: (id: string) => toggleEnabled(internals, id),
    installFromGit: (repoUrl: string) => installFromGit(internals, repoUrl),
    linkLocal: (localPath: string) => linkLocal(internals, localPath),
    uninstallGit: (name: string) => uninstallGit(internals, name),
    setPluginScope,
    getPluginVersion,
    hasUpdate,
    pendingAction: (id: string) => pendingActions()[id] ?? null,
    failedAction: (id: string) => failedActionsByPlugin()[id] ?? null,
    errorFor: (id: string) => errorsByPlugin()[id] ?? null,
    clearError,
    retry: (id: string) => retry(internals, id),
    recover: (id: string) => recover(internals, id),
    refresh,
  }
}

export function usePlugins() {
  if (!store) store = createPluginsStore()
  return store
}
