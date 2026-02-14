import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

async function loadPluginsStore() {
  vi.resetModules()
  const mod = await import('./plugins')
  return mod.usePlugins()
}

describe('plugins store', () => {
  beforeEach(() => {
    localStorage.clear()
  })

  afterEach(() => {
    localStorage.clear()
  })

  it('loads catalog and featured plugins', async () => {
    const plugins = await loadPluginsStore()

    await plugins.loadCatalog()

    expect(plugins.catalog().length).toBeGreaterThan(0)
    expect(plugins.featuredPlugins().length).toBeGreaterThan(0)
  })

  it('filters by search query', async () => {
    const plugins = await loadPluginsStore()

    await plugins.loadCatalog()
    plugins.setSearchQuery('formatter')

    expect(
      plugins.filteredPlugins().every((plugin) => plugin.name.toLowerCase().includes('formatter'))
    ).toBe(true)
  })

  it('filters by category', async () => {
    const plugins = await loadPluginsStore()

    await plugins.loadCatalog()
    const category = plugins.categories()[0]
    plugins.setActiveCategory(category)

    expect(plugins.filteredPlugins().every((plugin) => plugin.category === category)).toBe(true)
  })

  it('installs and uninstalls plugin', async () => {
    const plugins = await loadPluginsStore()

    await plugins.loadCatalog()
    const target = plugins.catalog()[0]

    await plugins.installPlugin(target.id)
    expect(plugins.isInstalled(target.id)).toBe(true)

    await plugins.uninstallPlugin(target.id)
    expect(plugins.isInstalled(target.id)).toBe(false)
  })

  it('marks plugin settings target', async () => {
    const plugins = await loadPluginsStore()

    await plugins.loadCatalog()
    const target = plugins.catalog()[0]

    plugins.openPluginSettings(target.id)
    expect(plugins.settingsTargetPluginId()).toBe(target.id)

    plugins.clearPluginSettingsTarget()
    expect(plugins.settingsTargetPluginId()).toBeNull()
  })
})
