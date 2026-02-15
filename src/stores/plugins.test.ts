import { createRoot } from 'solid-js'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { resetPluginsStore, usePlugins } from './plugins'

const mocks = vi.hoisted(() => ({
  loadPluginsStateMock: vi.fn(async () => ({})),
  installPluginMock: vi.fn(async () => ({ installed: true, enabled: true })),
  uninstallPluginMock: vi.fn(async () => ({ installed: false, enabled: false })),
  setPluginEnabledMock: vi.fn(async (_pluginId: string, enabled: boolean) => ({
    installed: true,
    enabled,
  })),
}))

vi.mock('../services/plugins-fs', () => ({
  loadPluginsState: mocks.loadPluginsStateMock,
  installPlugin: mocks.installPluginMock,
  uninstallPlugin: mocks.uninstallPluginMock,
  setPluginEnabled: mocks.setPluginEnabledMock,
}))

describe('plugins store', () => {
  beforeEach(() => {
    resetPluginsStore()
    localStorage.clear()
    mocks.loadPluginsStateMock.mockResolvedValue({})
    mocks.installPluginMock.mockResolvedValue({ installed: true, enabled: true })
    mocks.uninstallPluginMock.mockResolvedValue({ installed: false, enabled: false })
    mocks.setPluginEnabledMock.mockImplementation(async (_pluginId: string, enabled: boolean) => ({
      installed: true,
      enabled,
    }))
  })

  afterEach(() => {
    resetPluginsStore()
    localStorage.clear()
  })

  it('installs, toggles, and uninstalls plugin state', async () => {
    await new Promise<void>((resolve) => {
      createRoot((dispose) => {
        void (async () => {
          const plugins = usePlugins()

          await plugins.install('task-planner')
          expect(plugins.pluginState()['task-planner']).toEqual({ installed: true, enabled: true })

          await plugins.toggleEnabled('task-planner')
          expect(plugins.pluginState()['task-planner']).toEqual({ installed: true, enabled: false })

          await plugins.uninstall('task-planner')
          expect(plugins.pluginState()['task-planner']).toBeUndefined()

          dispose()
          resolve()
        })()
      })
    })
  })

  it('filters list by installed status and search query', async () => {
    await new Promise<void>((resolve) => {
      createRoot((dispose) => {
        void (async () => {
          const plugins = usePlugins()
          await plugins.install('task-planner')

          plugins.setShowInstalledOnly(true)
          expect(plugins.filteredPlugins().some((p) => p.id === 'task-planner')).toBe(true)
          expect(plugins.filteredPlugins().some((p) => p.id === 'test-guard')).toBe(false)

          plugins.setShowInstalledOnly(false)
          plugins.setSearch('mcp')
          expect(plugins.filteredPlugins().map((p) => p.id)).toEqual(['mcp-inspector'])

          dispose()
          resolve()
        })()
      })
    })
  })

  it('filters list by category', async () => {
    await new Promise<void>((resolve) => {
      createRoot((dispose) => {
        void (async () => {
          const plugins = usePlugins()

          plugins.setCategoryFilter('integration')
          expect(plugins.filteredPlugins().map((p) => p.id)).toEqual(['mcp-inspector'])

          plugins.setCategoryFilter('all')
          expect(plugins.filteredPlugins().length).toBeGreaterThan(1)

          dispose()
          resolve()
        })()
      })
    })
  })

  it('exposes metadata for featured plugins', () => {
    createRoot((dispose) => {
      const plugins = usePlugins()
      const featured = plugins.featuredPlugins()
      expect(featured.length).toBeGreaterThan(0)
      expect(featured[0]?.version).toBeTruthy()
      expect(featured[0]?.trust).toBeTruthy()
      expect(featured[0]?.source).toBeTruthy()

      dispose()
    })
  })

  it('captures an error when enabling a non-installed plugin', async () => {
    await new Promise<void>((resolve) => {
      createRoot((dispose) => {
        void (async () => {
          const plugins = usePlugins()

          await plugins.toggleEnabled('task-planner')
          expect(plugins.errorFor('task-planner')).toContain('installed')

          dispose()
          resolve()
        })()
      })
    })
  })

  it('restores previous state when install fails', async () => {
    await new Promise<void>((resolve) => {
      createRoot((dispose) => {
        void (async () => {
          const plugins = usePlugins()
          mocks.installPluginMock.mockRejectedValueOnce(new Error('install failed'))

          await plugins.install('task-planner')

          expect(plugins.pluginState()['task-planner']).toBeUndefined()
          expect(plugins.errorFor('task-planner')).toContain('install failed')

          dispose()
          resolve()
        })()
      })
    })
  })

  it('retries failed install action', async () => {
    await new Promise<void>((resolve) => {
      createRoot((dispose) => {
        void (async () => {
          const plugins = usePlugins()
          mocks.installPluginMock.mockRejectedValueOnce(new Error('transient failure'))

          await plugins.install('task-planner')
          expect(plugins.errorFor('task-planner')).toContain('transient failure')
          expect(plugins.failedAction('task-planner')).toBe('install')

          await plugins.retry('task-planner')

          expect(plugins.errorFor('task-planner')).toBeNull()
          expect(plugins.failedAction('task-planner')).toBeNull()
          expect(plugins.pluginState()['task-planner']).toEqual({ installed: true, enabled: true })

          dispose()
          resolve()
        })()
      })
    })
  })
})
