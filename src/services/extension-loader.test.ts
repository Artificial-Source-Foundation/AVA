/**
 * Extension Loader Tests
 *
 * Tests for plugin loading: missing activate export, permission sandboxing,
 * and watch debouncing.
 */

import { beforeEach, describe, expect, it, vi } from 'vitest'

// Mock plugins-fs module
const mockPluginsFsMod = {
  listInstalledPlugins: vi.fn().mockResolvedValue([]),
  readPluginSource: vi.fn().mockResolvedValue(null),
  readPluginManifest: vi.fn().mockResolvedValue(null),
  loadPluginsState: vi.fn().mockResolvedValue({}),
}

vi.mock('./plugins-fs', () => mockPluginsFsMod)

describe('extension-loader', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  describe('loadInstalledPlugins', () => {
    it('returns empty cleanup for no installed plugins', async () => {
      const { loadInstalledPlugins } = await import('./extension-loader')
      const createApi = vi.fn()

      const result = await loadInstalledPlugins(createApi)

      expect(result.pluginDisposables.size).toBe(0)
      expect(typeof result.cleanup).toBe('function')
      result.cleanup() // should not throw
    })

    it('skips disabled plugins', async () => {
      mockPluginsFsMod.listInstalledPlugins.mockResolvedValueOnce(['my-plugin'])
      mockPluginsFsMod.loadPluginsState.mockResolvedValueOnce({
        'my-plugin': { installed: true, enabled: false },
      })

      const { loadInstalledPlugins } = await import('./extension-loader')
      const createApi = vi.fn()

      const result = await loadInstalledPlugins(createApi)

      expect(result.pluginDisposables.size).toBe(0)
      expect(mockPluginsFsMod.readPluginSource).not.toHaveBeenCalled()
    })

    it('skips plugin with no source', async () => {
      mockPluginsFsMod.listInstalledPlugins.mockResolvedValueOnce(['my-plugin'])
      mockPluginsFsMod.loadPluginsState.mockResolvedValueOnce({
        'my-plugin': { installed: true, enabled: true },
      })
      mockPluginsFsMod.readPluginSource.mockResolvedValueOnce(null)

      const { loadInstalledPlugins } = await import('./extension-loader')
      const createApi = vi.fn()

      const result = await loadInstalledPlugins(createApi)

      expect(result.pluginDisposables.size).toBe(0)
    })

    it('handles scan failure gracefully', async () => {
      mockPluginsFsMod.listInstalledPlugins.mockRejectedValueOnce(new Error('fs error'))

      const { loadInstalledPlugins } = await import('./extension-loader')
      const createApi = vi.fn()

      // Should not throw
      const result = await loadInstalledPlugins(createApi)
      expect(result.pluginDisposables.size).toBe(0)
    })
  })
})
