import { createMemo, createSignal } from 'solid-js'
import {
  installPlugin,
  loadPluginsState,
  setPluginEnabled,
  uninstallPlugin,
} from '../services/plugins-fs'
import type { PluginCatalogItem, PluginState } from '../types/plugin'
import {
  FEATURED_PLUGIN_IDS,
  PLUGIN_CATALOG,
  type PluginCatalogStatus,
  syncPluginCatalog,
} from './plugins-catalog'

type PluginAction = 'install' | 'uninstall' | 'toggle'
type PluginCategoryFilter = PluginCatalogItem['category'] | 'all'

let store: ReturnType<typeof createPluginsStore> | null = null

export function resetPluginsStore() {
  store = null
}

function createPluginsStore() {
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

  const setState = (id: string, next: PluginState) => {
    const updated = { ...pluginState(), [id]: next }
    setPluginState(updated)
  }

  const clearState = (id: string) => {
    const current = pluginState()
    if (!(id in current)) return

    const next = { ...current }
    delete next[id]
    setPluginState(next)
  }

  const setPendingAction = (id: string, action: PluginAction | null) => {
    setPendingActions((prev) => ({ ...prev, [id]: action }))
  }

  const setError = (id: string, message: string) => {
    setErrorsByPlugin((prev) => ({ ...prev, [id]: message }))
  }

  const clearError = (id: string) => {
    setErrorsByPlugin((prev) => {
      if (!(id in prev)) return prev
      const next = { ...prev }
      delete next[id]
      return next
    })
  }

  const setFailedAction = (id: string, action: PluginAction | null) => {
    setFailedActionsByPlugin((prev) => ({ ...prev, [id]: action }))
  }

  const runLifecycle = <T>(operation: () => Promise<T>) => {
    const task = lifecycleQueue.then(operation, operation)
    lifecycleQueue = task.then(
      () => undefined,
      () => undefined
    )
    return task
  }

  const install = async (id: string) => {
    if (pendingActions()[id]) return

    const previous = pluginState()[id]
    clearError(id)
    setPendingAction(id, 'install')
    setState(id, { installed: true, enabled: true })

    try {
      const next = await runLifecycle(() => installPlugin(id))
      setState(id, next)
      setFailedAction(id, null)
    } catch (error) {
      if (previous) {
        setState(id, previous)
      } else {
        clearState(id)
      }
      setFailedAction(id, 'install')
      setError(id, error instanceof Error ? error.message : 'Failed to install plugin.')
    } finally {
      setPendingAction(id, null)
    }
  }

  const uninstall = async (id: string) => {
    if (pendingActions()[id]) return

    const previous = pluginState()[id] ?? { installed: false, enabled: false }
    if (!previous.installed) {
      setError(id, 'Plugin is not installed.')
      return
    }

    clearError(id)
    setPendingAction(id, 'uninstall')
    clearState(id)

    try {
      const next = await runLifecycle(() => uninstallPlugin(id))
      if (next.installed) {
        setState(id, next)
      } else {
        clearState(id)
      }
      setFailedAction(id, null)
    } catch (error) {
      setState(id, previous)
      setFailedAction(id, 'uninstall')
      setError(id, error instanceof Error ? error.message : 'Failed to uninstall plugin.')
    } finally {
      setPendingAction(id, null)
    }
  }

  const toggleEnabled = async (id: string) => {
    if (pendingActions()[id]) return

    const current = pluginState()[id] ?? { installed: false, enabled: false }
    if (!current.installed) {
      setError(id, 'Plugin must be installed before enabling or disabling.')
      return
    }

    const optimistic = { ...current, enabled: !current.enabled }
    clearError(id)
    setPendingAction(id, 'toggle')
    setState(id, optimistic)

    try {
      const next = await runLifecycle(() => setPluginEnabled(id, optimistic.enabled))
      setState(id, next)
      setFailedAction(id, null)
    } catch (error) {
      setState(id, current)
      setFailedAction(id, 'toggle')
      setError(id, error instanceof Error ? error.message : 'Failed to update plugin state.')
    } finally {
      setPendingAction(id, null)
    }
  }

  const retry = async (id: string) => {
    const failedAction = failedActionsByPlugin()[id]
    if (!failedAction || pendingActions()[id]) return

    if (failedAction === 'install') {
      await install(id)
      return
    }

    if (failedAction === 'uninstall') {
      await uninstall(id)
      return
    }

    await toggleEnabled(id)
  }

  const recover = async (id: string) => {
    const current = pluginState()[id] ?? { installed: false, enabled: false }
    if (current.installed) {
      await uninstall(id)
      return
    }

    clearError(id)
    setFailedAction(id, null)
  }

  const installedCount = createMemo(
    () => Object.values(pluginState()).filter((entry) => entry.installed).length
  )

  const refresh = async () => {
    const state = await loadPluginsState()
    setPluginState(state)
  }

  const syncCatalog = async () => {
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

  void refresh()

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
    setSearch,
    setShowInstalledOnly,
    setCategoryFilter,
    syncCatalog,
    install,
    uninstall,
    toggleEnabled,
    pendingAction: (id: string) => pendingActions()[id] ?? null,
    failedAction: (id: string) => failedActionsByPlugin()[id] ?? null,
    errorFor: (id: string) => errorsByPlugin()[id] ?? null,
    clearError,
    retry,
    recover,
    refresh,
  }
}

export function usePlugins() {
  if (!store) store = createPluginsStore()
  return store
}
