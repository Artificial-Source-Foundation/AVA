import { createMemo, createSignal } from 'solid-js'
import { listPluginCatalog } from '../services/plugins/catalog'
import {
  installPlugin as installPluginInLifecycle,
  listInstalledPlugins,
  uninstallPlugin as uninstallPluginInLifecycle,
} from '../services/plugins/lifecycle'
import type { PluginManifest } from '../types'

const [catalog, setCatalog] = createSignal<PluginManifest[]>([])
const [isCatalogLoading, setIsCatalogLoading] = createSignal(false)
const [searchQuery, setSearchQuery] = createSignal('')
const [activeCategory, setActiveCategory] = createSignal<string>('all')
const [installedPluginIds, setInstalledPluginIds] = createSignal<string[]>([])
const [pendingPluginIds, setPendingPluginIds] = createSignal<string[]>([])
const [settingsTargetPluginId, setSettingsTargetPluginId] = createSignal<string | null>(null)

const filteredPlugins = createMemo(() => {
  const query = searchQuery().trim().toLowerCase()

  return catalog().filter((plugin) => {
    const categoryMatch = activeCategory() === 'all' || plugin.category === activeCategory()
    if (!categoryMatch) {
      return false
    }

    if (!query) {
      return true
    }

    return (
      plugin.name.toLowerCase().includes(query) ||
      plugin.description.toLowerCase().includes(query) ||
      plugin.tags.some((tag) => tag.toLowerCase().includes(query))
    )
  })
})

const featuredPlugins = createMemo(() => filteredPlugins().filter((plugin) => plugin.featured))

const categories = createMemo(() => {
  const values = new Set<string>()
  for (const plugin of catalog()) {
    values.add(plugin.category)
  }
  return [...values].sort()
})

function withPendingPlugin(pluginId: string, task: () => Promise<void>): Promise<void> {
  setPendingPluginIds((prev) => [...new Set([...prev, pluginId])])

  return task().finally(() => {
    setPendingPluginIds((prev) => prev.filter((id) => id !== pluginId))
  })
}

export function usePlugins() {
  return {
    catalog,
    filteredPlugins,
    featuredPlugins,
    categories,
    isCatalogLoading,
    searchQuery,
    activeCategory,
    pendingPluginIds,
    settingsTargetPluginId,

    loadCatalog: async () => {
      setIsCatalogLoading(true)
      try {
        const [plugins, installed] = await Promise.all([
          listPluginCatalog(),
          listInstalledPlugins(),
        ])
        setCatalog(plugins)
        setInstalledPluginIds(installed)
      } finally {
        setIsCatalogLoading(false)
      }
    },

    setSearchQuery,
    setActiveCategory,

    isInstalled: (pluginId: string): boolean => {
      return installedPluginIds().includes(pluginId)
    },

    isPending: (pluginId: string): boolean => {
      return pendingPluginIds().includes(pluginId)
    },

    installPlugin: async (pluginId: string): Promise<void> => {
      if (installedPluginIds().includes(pluginId)) {
        return
      }

      await withPendingPlugin(pluginId, async () => {
        await installPluginInLifecycle(pluginId)
        setInstalledPluginIds((prev) => [...new Set([...prev, pluginId])])
      })
    },

    uninstallPlugin: async (pluginId: string): Promise<void> => {
      if (!installedPluginIds().includes(pluginId)) {
        return
      }

      await withPendingPlugin(pluginId, async () => {
        await uninstallPluginInLifecycle(pluginId)
        setInstalledPluginIds((prev) => prev.filter((id) => id !== pluginId))
      })
    },

    openPluginSettings: (pluginId: string): void => {
      setSettingsTargetPluginId(pluginId)
    },

    clearPluginSettingsTarget: (): void => {
      setSettingsTargetPluginId(null)
    },
  }
}
