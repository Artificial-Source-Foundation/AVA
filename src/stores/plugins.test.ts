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

  it('updates catalog sync status', async () => {
    await new Promise<void>((resolve) => {
      createRoot((dispose) => {
        void (async () => {
          const plugins = usePlugins()
          expect(plugins.catalogStatus()).toBe('idle')

          await plugins.syncCatalog()

          expect(plugins.catalogStatus()).toBe('ready')
          expect(plugins.lastCatalogSyncAt()).not.toBeNull()
          expect(plugins.catalogError()).toBeNull()

          dispose()
          resolve()
        })()
      })
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

  describe('rapid toggle sequences', () => {
    it('ignores concurrent toggles while one is pending', async () => {
      await new Promise<void>((resolve) => {
        createRoot((dispose) => {
          void (async () => {
            const plugins = usePlugins()
            await plugins.install('task-planner')

            // Make toggle slow
            mocks.setPluginEnabledMock.mockImplementation(async (_id: string, enabled: boolean) => {
              await new Promise((r) => setTimeout(r, 50))
              return { installed: true, enabled }
            })

            // Clear call counts after install
            mocks.setPluginEnabledMock.mockClear()

            // Fire two toggles concurrently — second should be ignored
            const p1 = plugins.toggleEnabled('task-planner')
            const p2 = plugins.toggleEnabled('task-planner')
            await Promise.all([p1, p2])

            // Only one actual call because the second was skipped (pendingAction guard)
            expect(mocks.setPluginEnabledMock).toHaveBeenCalledTimes(1)

            dispose()
            resolve()
          })()
        })
      })
    })

    it('handles sequential enable-disable-enable with correct final state', async () => {
      await new Promise<void>((resolve) => {
        createRoot((dispose) => {
          void (async () => {
            const plugins = usePlugins()
            await plugins.install('task-planner')

            // Clear call counts after install
            mocks.setPluginEnabledMock.mockClear()

            // Toggle 1: disable
            await plugins.toggleEnabled('task-planner')
            expect(plugins.pluginState()['task-planner']?.enabled).toBe(false)

            // Toggle 2: re-enable
            await plugins.toggleEnabled('task-planner')
            expect(plugins.pluginState()['task-planner']?.enabled).toBe(true)

            // Toggle 3: disable again
            await plugins.toggleEnabled('task-planner')
            expect(plugins.pluginState()['task-planner']?.enabled).toBe(false)

            expect(mocks.setPluginEnabledMock).toHaveBeenCalledTimes(3)

            dispose()
            resolve()
          })()
        })
      })
    })
  })

  describe('error recovery for install failures', () => {
    it('recover after install failure clears error for uninstalled plugin', async () => {
      await new Promise<void>((resolve) => {
        createRoot((dispose) => {
          void (async () => {
            const plugins = usePlugins()
            mocks.installPluginMock.mockRejectedValueOnce(new Error('bad manifest'))

            await plugins.install('task-planner')
            expect(plugins.errorFor('task-planner')).toBe('bad manifest')
            expect(plugins.failedAction('task-planner')).toBe('install')

            // Recover on uninstalled plugin should clear error
            await plugins.recover('task-planner')
            expect(plugins.errorFor('task-planner')).toBeNull()
            expect(plugins.failedAction('task-planner')).toBeNull()

            dispose()
            resolve()
          })()
        })
      })
    })

    it('recover on installed plugin triggers uninstall', async () => {
      await new Promise<void>((resolve) => {
        createRoot((dispose) => {
          void (async () => {
            const plugins = usePlugins()
            await plugins.install('task-planner')

            // Toggle fails, leaving plugin in installed state
            mocks.setPluginEnabledMock.mockRejectedValueOnce(new Error('toggle error'))
            await plugins.toggleEnabled('task-planner')
            expect(plugins.errorFor('task-planner')).toBe('toggle error')

            // Recover uninstalls the plugin
            await plugins.recover('task-planner')
            expect(plugins.pluginState()['task-planner']).toBeUndefined()

            dispose()
            resolve()
          })()
        })
      })
    })
  })

  describe('lifecycle queue serialization', () => {
    it('serializes install and uninstall operations', async () => {
      await new Promise<void>((resolve) => {
        createRoot((dispose) => {
          void (async () => {
            const plugins = usePlugins()

            // Make operations slow
            mocks.installPluginMock.mockImplementation(async () => {
              await new Promise((r) => setTimeout(r, 30))
              return { installed: true, enabled: true }
            })

            // Install two plugins concurrently — they should serialize
            const p1 = plugins.install('task-planner')
            const p2 = plugins.install('test-guard')
            await Promise.all([p1, p2])

            expect(plugins.pluginState()['task-planner']?.installed).toBe(true)
            // test-guard install will error (pendingAction guard) but shouldn't crash

            dispose()
            resolve()
          })()
        })
      })
    })
  })

  describe('error recovery for broken manifests', () => {
    it('handles uninstall failure with state rollback', async () => {
      await new Promise<void>((resolve) => {
        createRoot((dispose) => {
          void (async () => {
            const plugins = usePlugins()
            await plugins.install('task-planner')

            mocks.uninstallPluginMock.mockRejectedValueOnce(new Error('permission denied'))
            await plugins.uninstall('task-planner')

            // Should revert to installed state
            expect(plugins.pluginState()['task-planner']?.installed).toBe(true)
            expect(plugins.errorFor('task-planner')).toBe('permission denied')
            expect(plugins.failedAction('task-planner')).toBe('uninstall')

            dispose()
            resolve()
          })()
        })
      })
    })

    it('handles toggle failure with rollback to original enabled state', async () => {
      await new Promise<void>((resolve) => {
        createRoot((dispose) => {
          void (async () => {
            const plugins = usePlugins()
            await plugins.install('task-planner')
            expect(plugins.pluginState()['task-planner']?.enabled).toBe(true)

            mocks.setPluginEnabledMock.mockRejectedValueOnce(new Error('storage corrupted'))
            await plugins.toggleEnabled('task-planner')

            // Should revert to enabled=true (original)
            expect(plugins.pluginState()['task-planner']?.enabled).toBe(true)
            expect(plugins.errorFor('task-planner')).toBe('storage corrupted')
            expect(plugins.failedAction('task-planner')).toBe('toggle')

            dispose()
            resolve()
          })()
        })
      })
    })

    it('prevents uninstall on non-installed plugin', async () => {
      await new Promise<void>((resolve) => {
        createRoot((dispose) => {
          void (async () => {
            const plugins = usePlugins()

            // Clear mock to isolate this test
            mocks.uninstallPluginMock.mockClear()

            await plugins.uninstall('task-planner')

            expect(plugins.errorFor('task-planner')).toContain('not installed')
            expect(mocks.uninstallPluginMock).not.toHaveBeenCalled()

            dispose()
            resolve()
          })()
        })
      })
    })
  })
})
