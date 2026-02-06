/**
 * Extension Manager Tests
 */

import { existsSync } from 'node:fs'
import { mkdir, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { ExtensionManager, getExtensionManager, resetExtensionManager } from './manager.js'
import { CONFIG_FILENAME } from './manifest.js'
import type { ExtensionEvent } from './types.js'

describe('ExtensionManager', () => {
  let testDir: string
  let extensionsDir: string
  let enablementPath: string
  let manager: ExtensionManager

  beforeEach(async () => {
    testDir = join(
      tmpdir(),
      `estela-ext-mgr-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`
    )
    extensionsDir = join(testDir, 'extensions')
    enablementPath = join(testDir, 'enablement.json')
    await mkdir(extensionsDir, { recursive: true })

    manager = new ExtensionManager({
      workspaceDir: testDir,
      extensionsDir,
      enablementPath,
    })
  })

  afterEach(async () => {
    resetExtensionManager()
    try {
      await rm(testDir, { recursive: true })
    } catch {
      // Ignore
    }
  })

  /**
   * Helper: Create an extension directory with config
   */
  async function createExtension(
    name: string,
    version = '1.0.0',
    extra: Record<string, unknown> = {}
  ): Promise<string> {
    const extDir = join(extensionsDir, name)
    await mkdir(extDir, { recursive: true })
    const config = { name, version, ...extra }
    await writeFile(join(extDir, CONFIG_FILENAME), JSON.stringify(config), 'utf-8')
    return extDir
  }

  /**
   * Helper: Create an extension source directory (for install tests)
   */
  async function createExtensionSource(
    name: string,
    version = '1.0.0',
    extra: Record<string, unknown> = {}
  ): Promise<string> {
    const sourceDir = join(testDir, 'sources', name)
    await mkdir(sourceDir, { recursive: true })
    const config = { name, version, ...extra }
    await writeFile(join(sourceDir, CONFIG_FILENAME), JSON.stringify(config), 'utf-8')
    return sourceDir
  }

  // =========================================================================
  // Loading
  // =========================================================================

  describe('loadExtensions', () => {
    it('should load extensions from directory', async () => {
      await createExtension('ext-a', '1.0.0')
      await createExtension('ext-b', '2.0.0')

      const extensions = await manager.loadExtensions()
      expect(extensions).toHaveLength(2)

      const names = extensions.map((e) => e.name).sort()
      expect(names).toEqual(['ext-a', 'ext-b'])
    })

    it('should return empty array for empty directory', async () => {
      const extensions = await manager.loadExtensions()
      expect(extensions).toHaveLength(0)
    })

    it('should return empty array if directory does not exist', async () => {
      const mgr = new ExtensionManager({
        extensionsDir: join(testDir, 'nonexistent'),
        enablementPath,
      })
      const extensions = await mgr.loadExtensions()
      expect(extensions).toHaveLength(0)
    })

    it('should skip directories without config files', async () => {
      await createExtension('valid-ext')
      await mkdir(join(extensionsDir, 'no-config'), { recursive: true })

      const extensions = await manager.loadExtensions()
      expect(extensions).toHaveLength(1)
      expect(extensions[0]!.name).toBe('valid-ext')
    })

    it('should skip extensions with invalid configs', async () => {
      await createExtension('valid-ext')
      const invalidDir = join(extensionsDir, 'bad-ext')
      await mkdir(invalidDir, { recursive: true })
      await writeFile(join(invalidDir, CONFIG_FILENAME), 'not json', 'utf-8')

      const extensions = await manager.loadExtensions()
      expect(extensions).toHaveLength(1)
    })

    it('should not reload if already loaded', async () => {
      await createExtension('ext-a')

      const first = await manager.loadExtensions()
      await createExtension('ext-b')
      const second = await manager.loadExtensions()

      // Second call returns cached result
      expect(first).toHaveLength(1)
      expect(second).toHaveLength(1)
    })

    it('should emit loaded event', async () => {
      await createExtension('ext-a')
      const events: ExtensionEvent[] = []
      manager.on((e) => events.push(e))

      await manager.loadExtensions()

      expect(events).toHaveLength(1)
      expect(events[0]!.type).toBe('loaded')
    })
  })

  // =========================================================================
  // Getters
  // =========================================================================

  describe('getters', () => {
    it('should return all extensions', async () => {
      await createExtension('ext-a')
      await createExtension('ext-b')
      await manager.loadExtensions()

      expect(manager.getExtensions()).toHaveLength(2)
    })

    it('should return only active extensions', async () => {
      await createExtension('ext-a')
      await createExtension('ext-b')
      await manager.loadExtensions()
      await manager.disable('ext-b')

      const active = manager.getActiveExtensions()
      expect(active).toHaveLength(1)
      expect(active[0]!.name).toBe('ext-a')
    })

    it('should find extension by name (case-insensitive)', async () => {
      await createExtension('My-Extension')
      await manager.loadExtensions()

      expect(manager.findExtension('my-extension')).toBeDefined()
      expect(manager.findExtension('MY-EXTENSION')).toBeDefined()
      expect(manager.findExtension('nonexistent')).toBeUndefined()
    })

    it('should track size', async () => {
      expect(manager.size).toBe(0)
      await createExtension('ext-a')
      await manager.loadExtensions()
      expect(manager.size).toBe(1)
    })

    it('should track loaded state', async () => {
      expect(manager.isLoaded).toBe(false)
      await manager.loadExtensions()
      expect(manager.isLoaded).toBe(true)
    })
  })

  // =========================================================================
  // Install
  // =========================================================================

  describe('install', () => {
    it('should install from local path', async () => {
      await manager.loadExtensions()
      const source = await createExtensionSource('new-ext', '1.0.0')

      const extension = await manager.install(source)

      expect(extension.name).toBe('new-ext')
      expect(extension.version).toBe('1.0.0')
      expect(extension.isActive).toBe(true)
      expect(manager.size).toBe(1)
    })

    it('should persist installed extension to disk', async () => {
      await manager.loadExtensions()
      const source = await createExtensionSource('persistent-ext')

      await manager.install(source)

      // Verify files exist on disk
      expect(existsSync(join(extensionsDir, 'persistent-ext', CONFIG_FILENAME))).toBe(true)
    })

    it('should emit installed event', async () => {
      await manager.loadExtensions()
      const source = await createExtensionSource('evt-ext')
      const events: ExtensionEvent[] = []
      manager.on((e) => events.push(e))

      await manager.install(source)

      const installEvent = events.find((e) => e.type === 'installed')
      expect(installEvent).toBeDefined()
    })

    it('should reject duplicate installs', async () => {
      await manager.loadExtensions()
      const source = await createExtensionSource('dupe-ext')

      await manager.install(source)
      await expect(manager.install(source)).rejects.toThrow('already installed')
    })

    it('should reject missing source', async () => {
      await manager.loadExtensions()
      await expect(manager.install('/nonexistent/path')).rejects.toThrow('not found')
    })

    it('should reject source without config', async () => {
      await manager.loadExtensions()
      const sourceDir = join(testDir, 'sources', 'no-config')
      await mkdir(sourceDir, { recursive: true })

      await expect(manager.install(sourceDir)).rejects.toThrow(CONFIG_FILENAME)
    })

    it('should install with context files', async () => {
      await manager.loadExtensions()
      const source = await createExtensionSource('ctx-ext', '1.0.0', {
        contextFiles: ['README.md'],
      })
      await writeFile(join(source, 'README.md'), '# Context', 'utf-8')

      const extension = await manager.install(source)
      expect(extension.contextFiles).toHaveLength(1)
    })

    it('should install with MCP servers', async () => {
      await manager.loadExtensions()
      const source = await createExtensionSource('mcp-ext', '1.0.0', {
        mcpServers: {
          'my-server': { type: 'stdio', command: 'node', args: ['server.js'] },
        },
      })

      const extension = await manager.install(source)
      expect(extension.mcpServers).toBeDefined()
      expect(extension.mcpServers!['my-server']).toBeDefined()
    })
  })

  // =========================================================================
  // Uninstall
  // =========================================================================

  describe('uninstall', () => {
    it('should uninstall an extension', async () => {
      await manager.loadExtensions()
      const source = await createExtensionSource('removable-ext')
      await manager.install(source)
      expect(manager.size).toBe(1)

      await manager.uninstall('removable-ext')
      expect(manager.size).toBe(0)
      expect(existsSync(join(extensionsDir, 'removable-ext'))).toBe(false)
    })

    it('should emit uninstalled event', async () => {
      await manager.loadExtensions()
      const source = await createExtensionSource('evt-ext')
      await manager.install(source)

      const events: ExtensionEvent[] = []
      manager.on((e) => events.push(e))

      await manager.uninstall('evt-ext')

      const uninstallEvent = events.find((e) => e.type === 'uninstalled')
      expect(uninstallEvent).toBeDefined()
    })

    it('should throw for unknown extension', async () => {
      await manager.loadExtensions()
      await expect(manager.uninstall('ghost')).rejects.toThrow('not installed')
    })
  })

  // =========================================================================
  // Enable / Disable
  // =========================================================================

  describe('enable/disable', () => {
    it('should disable an extension', async () => {
      await createExtension('ext-a')
      await manager.loadExtensions()

      await manager.disable('ext-a')

      const ext = manager.findExtension('ext-a')
      expect(ext!.isActive).toBe(false)
      expect(manager.isEnabled('ext-a')).toBe(false)
    })

    it('should re-enable a disabled extension', async () => {
      await createExtension('ext-a')
      await manager.loadExtensions()

      await manager.disable('ext-a')
      await manager.enable('ext-a')

      const ext = manager.findExtension('ext-a')
      expect(ext!.isActive).toBe(true)
    })

    it('should persist enablement state', async () => {
      await createExtension('ext-a')
      await manager.loadExtensions()
      await manager.disable('ext-a')

      // Create new manager pointing to same storage
      const mgr2 = new ExtensionManager({
        extensionsDir,
        enablementPath,
      })
      await mgr2.loadExtensions()

      expect(mgr2.isEnabled('ext-a')).toBe(false)
    })

    it('should emit enable/disable events', async () => {
      await createExtension('ext-a')
      await manager.loadExtensions()
      const events: ExtensionEvent[] = []
      manager.on((e) => events.push(e))

      await manager.disable('ext-a')
      await manager.enable('ext-a')

      expect(events.filter((e) => e.type === 'disabled')).toHaveLength(1)
      expect(events.filter((e) => e.type === 'enabled')).toHaveLength(1)
    })

    it('should throw when enabling unknown extension', async () => {
      await manager.loadExtensions()
      await expect(manager.enable('ghost')).rejects.toThrow('not installed')
    })

    it('should default to enabled for unknown names', () => {
      expect(manager.isEnabled('anything')).toBe(true)
    })
  })

  // =========================================================================
  // Events
  // =========================================================================

  describe('events', () => {
    it('should support unsubscribing', async () => {
      await createExtension('ext-a')
      const events: ExtensionEvent[] = []
      const unsub = manager.on((e) => events.push(e))

      await manager.loadExtensions()
      expect(events).toHaveLength(1)

      unsub()
      await manager.reload()
      // Should still be 1 since we unsubscribed before reload
      // (reload emits 'loaded', but we unsubbed)
      expect(events).toHaveLength(1)
    })

    it('should not crash on listener errors', async () => {
      manager.on(() => {
        throw new Error('bad listener')
      })

      // Should not throw
      await createExtension('ext-a')
      await manager.loadExtensions()
    })
  })

  // =========================================================================
  // Reload
  // =========================================================================

  describe('reload', () => {
    it('should reload extensions from disk', async () => {
      await createExtension('ext-a')
      await manager.loadExtensions()
      expect(manager.size).toBe(1)

      // Add another extension on disk
      await createExtension('ext-b')

      const reloaded = await manager.reload()
      expect(reloaded).toHaveLength(2)
      expect(manager.size).toBe(2)
    })
  })

  // =========================================================================
  // Reset
  // =========================================================================

  describe('reset', () => {
    it('should clear all state', async () => {
      await createExtension('ext-a')
      await manager.loadExtensions()

      manager.reset()

      expect(manager.size).toBe(0)
      expect(manager.isLoaded).toBe(false)
    })
  })

  // =========================================================================
  // Singleton
  // =========================================================================

  describe('singleton', () => {
    it('should return same instance', () => {
      const a = getExtensionManager()
      const b = getExtensionManager()
      expect(a).toBe(b)
    })

    it('should reset singleton', () => {
      const a = getExtensionManager()
      resetExtensionManager()
      const b = getExtensionManager()
      expect(a).not.toBe(b)
    })
  })
})
