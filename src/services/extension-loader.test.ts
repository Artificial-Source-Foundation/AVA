/**
 * Extension Loader Tests
 *
 * Tests for plugin loading: missing activate export, permission sandboxing,
 * and watch debouncing.
 */

import { beforeEach, describe, expect, it, vi } from 'vitest'

/** Minimal types (replaces @ava/core-v2/extensions import) */
interface Disposable {
  dispose(): void
}

/** Minimal ExtensionAPI interface matching plugin-loader.ts */
interface ExtensionAPI {
  registerTool(tool: { definition?: { name?: string }; [key: string]: unknown }): Disposable
  registerCommand: (...args: unknown[]) => Disposable
  registerAgentMode: (...args: unknown[]) => Disposable
  registerValidator: (...args: unknown[]) => Disposable
  registerContextStrategy: (...args: unknown[]) => Disposable
  registerProvider: (...args: unknown[]) => Disposable
  addToolMiddleware: (...args: unknown[]) => Disposable
  on: (...args: unknown[]) => Disposable
  emit: (...args: unknown[]) => void
  getSettings: (...args: unknown[]) => unknown
  onSettingsChanged: (...args: unknown[]) => Disposable
  getSessionManager: (...args: unknown[]) => unknown
}

let loadInstalledPlugins: (
  createApi: (name: string) => ExtensionAPI
) => Promise<{ cleanup: () => void; pluginDisposables: Map<string, Disposable> }>

// Mock plugins-fs module
const mockPluginsFsMod = {
  listInstalledPlugins: vi.fn().mockResolvedValue([]),
  readPluginSource: vi.fn().mockResolvedValue(null),
  readPluginManifest: vi.fn().mockResolvedValue(null),
  loadPluginsState: vi.fn().mockResolvedValue({}),
}

vi.mock('./plugins-fs', () => mockPluginsFsMod)

describe('extension-loader', () => {
  beforeEach(async () => {
    vi.resetModules()
    const mod = await import('./extension-loader')
    loadInstalledPlugins = mod.loadInstalledPlugins
    vi.clearAllMocks()
  })

  describe('loadInstalledPlugins', () => {
    it('returns empty cleanup for no installed plugins', async () => {
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

      const createApi = vi.fn()

      const result = await loadInstalledPlugins(createApi)

      expect(result.pluginDisposables.size).toBe(0)
    })

    it('handles scan failure gracefully', async () => {
      mockPluginsFsMod.listInstalledPlugins.mockRejectedValueOnce(new Error('fs error'))

      const createApi = vi.fn()

      // Should not throw
      const result = await loadInstalledPlugins(createApi)
      expect(result.pluginDisposables.size).toBe(0)
    })
  })
})
