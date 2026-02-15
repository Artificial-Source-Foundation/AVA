import { createRoot } from 'solid-js'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { resetPluginsStore, usePlugins } from './plugins'

describe('plugins store', () => {
  beforeEach(() => {
    resetPluginsStore()
    localStorage.clear()
  })

  afterEach(() => {
    resetPluginsStore()
    localStorage.clear()
  })

  it('installs, toggles, and uninstalls plugin state', () => {
    createRoot((dispose) => {
      const plugins = usePlugins()

      plugins.install('task-planner')
      expect(plugins.pluginState()['task-planner']).toEqual({ installed: true, enabled: true })

      plugins.toggleEnabled('task-planner')
      expect(plugins.pluginState()['task-planner']).toEqual({ installed: true, enabled: false })

      plugins.uninstall('task-planner')
      expect(plugins.pluginState()['task-planner']).toEqual({ installed: false, enabled: false })

      dispose()
    })
  })

  it('filters list by installed status and search query', () => {
    createRoot((dispose) => {
      const plugins = usePlugins()
      plugins.install('task-planner')

      plugins.setShowInstalledOnly(true)
      expect(plugins.filteredPlugins().some((p) => p.id === 'task-planner')).toBe(true)
      expect(plugins.filteredPlugins().some((p) => p.id === 'test-guard')).toBe(false)

      plugins.setShowInstalledOnly(false)
      plugins.setSearch('mcp')
      expect(plugins.filteredPlugins().map((p) => p.id)).toEqual(['mcp-inspector'])

      dispose()
    })
  })
})
