import { createMemo, createSignal } from 'solid-js'
import { loadPluginsState, savePluginsState } from '../services/plugins-fs'

export interface PluginCatalogItem {
  id: string
  name: string
  description: string
  category: 'workflow' | 'quality' | 'integration'
}

export interface PluginState {
  installed: boolean
  enabled: boolean
}

const PLUGIN_CATALOG: PluginCatalogItem[] = [
  {
    id: 'task-planner',
    name: 'Task Planner',
    description: 'Breaks goals into actionable implementation steps.',
    category: 'workflow',
  },
  {
    id: 'test-guard',
    name: 'Test Guard',
    description: 'Runs verification checks before completion.',
    category: 'quality',
  },
  {
    id: 'git-helper',
    name: 'Git Helper',
    description: 'Guides commit hygiene and pull request workflows.',
    category: 'workflow',
  },
  {
    id: 'mcp-inspector',
    name: 'MCP Inspector',
    description: 'Inspects and validates MCP server wiring.',
    category: 'integration',
  },
]

let store: ReturnType<typeof createPluginsStore> | null = null

export function resetPluginsStore() {
  store = null
}

function createPluginsStore() {
  const [search, setSearch] = createSignal('')
  const [showInstalledOnly, setShowInstalledOnly] = createSignal(false)
  const [pluginState, setPluginState] = createSignal<Record<string, PluginState>>({})

  const filteredPlugins = createMemo(() => {
    const query = search().trim().toLowerCase()
    return PLUGIN_CATALOG.filter((plugin) => {
      const state = pluginState()[plugin.id]
      if (showInstalledOnly() && !state?.installed) return false
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
    void savePluginsState(updated)
  }

  const install = (id: string) => setState(id, { installed: true, enabled: true })
  const uninstall = (id: string) => setState(id, { installed: false, enabled: false })
  const toggleEnabled = (id: string) => {
    const current = pluginState()[id] ?? { installed: false, enabled: false }
    if (!current.installed) return
    setState(id, { ...current, enabled: !current.enabled })
  }

  const installedCount = createMemo(
    () => Object.values(pluginState()).filter((entry) => entry.installed).length
  )

  const refresh = async () => {
    const state = await loadPluginsState()
    setPluginState(state)
  }

  void refresh()

  return {
    plugins: PLUGIN_CATALOG,
    filteredPlugins,
    search,
    showInstalledOnly,
    pluginState,
    installedCount,
    setSearch,
    setShowInstalledOnly,
    install,
    uninstall,
    toggleEnabled,
    refresh,
  }
}

export function usePlugins() {
  if (!store) store = createPluginsStore()
  return store
}
