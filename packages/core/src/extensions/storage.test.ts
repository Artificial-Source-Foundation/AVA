/**
 * Storage Tests
 */

import { existsSync } from 'node:fs'
import { mkdir, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { ExtensionStorage, loadEnablement, saveEnablement } from './storage.js'
import type { EnablementData, InstallMetadata } from './types.js'

describe('ExtensionStorage', () => {
  let testDir: string
  let extensionsDir: string

  beforeEach(async () => {
    testDir = join(
      tmpdir(),
      `ava-ext-storage-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`
    )
    extensionsDir = join(testDir, 'extensions')
    await mkdir(extensionsDir, { recursive: true })
  })

  afterEach(async () => {
    try {
      await rm(testDir, { recursive: true })
    } catch {
      // Ignore
    }
  })

  // =========================================================================
  // Extension Storage
  // =========================================================================

  describe('ExtensionStorage', () => {
    it('should construct with correct directory', () => {
      const storage = new ExtensionStorage('my-ext', extensionsDir)
      expect(storage.getExtensionDir()).toBe(join(extensionsDir, 'my-ext'))
    })

    it('should report exists correctly', async () => {
      const storage = new ExtensionStorage('my-ext', extensionsDir)
      expect(storage.exists()).toBe(false)

      await mkdir(storage.getExtensionDir(), { recursive: true })
      expect(storage.exists()).toBe(true)
    })

    it('should ensure directory creation', async () => {
      const storage = new ExtensionStorage('new-ext', extensionsDir)
      expect(storage.exists()).toBe(false)

      await storage.ensureDir()
      expect(storage.exists()).toBe(true)
    })

    it('should write and read metadata', async () => {
      const storage = new ExtensionStorage('my-ext', extensionsDir)
      const metadata: InstallMetadata = {
        type: 'local',
        source: '/path/to/source',
        installedAt: '2025-01-01T00:00:00.000Z',
      }

      await storage.writeMetadata(metadata)

      const loaded = storage.readMetadata()
      expect(loaded).toBeDefined()
      expect(loaded!.type).toBe('local')
      expect(loaded!.source).toBe('/path/to/source')
    })

    it('should return undefined for missing metadata', () => {
      const storage = new ExtensionStorage('nonexistent', extensionsDir)
      expect(storage.readMetadata()).toBeUndefined()
    })

    it('should remove extension directory', async () => {
      const storage = new ExtensionStorage('removable', extensionsDir)
      await storage.ensureDir()
      expect(storage.exists()).toBe(true)

      await storage.remove()
      expect(storage.exists()).toBe(false)
    })

    it('should handle remove when directory does not exist', async () => {
      const storage = new ExtensionStorage('ghost', extensionsDir)
      // Should not throw
      await storage.remove()
    })
  })

  // =========================================================================
  // Enablement Persistence
  // =========================================================================

  describe('enablement', () => {
    it('should save and load enablement data', async () => {
      const enablementPath = join(testDir, 'enablement.json')
      const data: EnablementData = {
        version: 1,
        extensions: { 'ext-a': true, 'ext-b': false },
      }

      await saveEnablement(data, enablementPath)
      const loaded = await loadEnablement(enablementPath)

      expect(loaded.version).toBe(1)
      expect(loaded.extensions['ext-a']).toBe(true)
      expect(loaded.extensions['ext-b']).toBe(false)
    })

    it('should return empty enablement for missing file', async () => {
      const enablementPath = join(testDir, 'missing-enablement.json')
      const loaded = await loadEnablement(enablementPath)

      expect(loaded.version).toBe(1)
      expect(loaded.extensions).toEqual({})
    })

    it('should return empty enablement for corrupted file', async () => {
      const enablementPath = join(testDir, 'bad-enablement.json')
      await writeFile(enablementPath, 'not json', 'utf-8')

      const loaded = await loadEnablement(enablementPath)
      expect(loaded.extensions).toEqual({})
    })

    it('should create parent directories when saving', async () => {
      const enablementPath = join(testDir, 'nested', 'deep', 'enablement.json')
      const data: EnablementData = { version: 1, extensions: { test: true } }

      await saveEnablement(data, enablementPath)
      expect(existsSync(enablementPath)).toBe(true)
    })
  })
})
