/**
 * Manifest Tests
 */

import { mkdir, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import {
  CONFIG_FILENAME,
  DEFAULT_CONTEXT_FILES,
  getContextFilePaths,
  INSTALL_METADATA_FILENAME,
  loadExtensionConfig,
  loadExtensionConfigSync,
  loadInstallMetadata,
  validateExtensionConfig,
  validateExtensionName,
} from './manifest.js'

describe('manifest', () => {
  let testDir: string

  beforeEach(async () => {
    testDir = join(
      tmpdir(),
      `estela-ext-manifest-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`
    )
    await mkdir(testDir, { recursive: true })
  })

  afterEach(async () => {
    try {
      await rm(testDir, { recursive: true })
    } catch {
      // Ignore
    }
  })

  // =========================================================================
  // Name Validation
  // =========================================================================

  describe('validateExtensionName', () => {
    it('should accept valid names', () => {
      expect(() => validateExtensionName('my-extension')).not.toThrow()
      expect(() => validateExtensionName('ext123')).not.toThrow()
      expect(() => validateExtensionName('a')).not.toThrow()
      expect(() => validateExtensionName('My-Extension-2')).not.toThrow()
    })

    it('should reject empty names', () => {
      expect(() => validateExtensionName('')).toThrow('required')
    })

    it('should reject names with special characters', () => {
      expect(() => validateExtensionName('my_extension')).toThrow('Invalid')
      expect(() => validateExtensionName('my extension')).toThrow('Invalid')
      expect(() => validateExtensionName('my.ext')).toThrow('Invalid')
      expect(() => validateExtensionName('@scope/pkg')).toThrow('Invalid')
    })

    it('should reject names starting with dash', () => {
      expect(() => validateExtensionName('-leading-dash')).toThrow('Invalid')
    })

    it('should reject names over 64 characters', () => {
      const longName = 'a'.repeat(65)
      expect(() => validateExtensionName(longName)).toThrow('too long')
    })
  })

  // =========================================================================
  // Config Validation
  // =========================================================================

  describe('validateExtensionConfig', () => {
    it('should accept valid config', () => {
      expect(() => validateExtensionConfig({ name: 'test', version: '1.0.0' })).not.toThrow()
    })

    it('should accept config with optional fields', () => {
      expect(() =>
        validateExtensionConfig({
          name: 'test',
          version: '1.0.0',
          description: 'A test extension',
          mcpServers: { server1: { type: 'stdio', command: 'node' } },
          contextFiles: ['CUSTOM.md'],
          excludeTools: ['bash'],
        })
      ).not.toThrow()
    })

    it('should reject null config', () => {
      expect(() => validateExtensionConfig(null)).toThrow('non-null object')
    })

    it('should reject config without name', () => {
      expect(() => validateExtensionConfig({ version: '1.0.0' })).toThrow(
        'missing required field "name"'
      )
    })

    it('should reject config without version', () => {
      expect(() => validateExtensionConfig({ name: 'test' })).toThrow(
        'missing required field "version"'
      )
    })
  })

  // =========================================================================
  // Config Loading
  // =========================================================================

  describe('loadExtensionConfig', () => {
    it('should load a valid config file', async () => {
      const config = { name: 'my-ext', version: '2.0.0', description: 'Test' }
      await writeFile(join(testDir, CONFIG_FILENAME), JSON.stringify(config), 'utf-8')

      const loaded = await loadExtensionConfig(testDir)
      expect(loaded.name).toBe('my-ext')
      expect(loaded.version).toBe('2.0.0')
      expect(loaded.description).toBe('Test')
    })

    it('should throw if config file missing', async () => {
      await expect(loadExtensionConfig(testDir)).rejects.toThrow('not found')
    })

    it('should throw if config is invalid JSON', async () => {
      await writeFile(join(testDir, CONFIG_FILENAME), 'not json', 'utf-8')
      await expect(loadExtensionConfig(testDir)).rejects.toThrow('Invalid JSON')
    })

    it('should throw if config is missing required fields', async () => {
      await writeFile(join(testDir, CONFIG_FILENAME), '{"name":"test"}', 'utf-8')
      await expect(loadExtensionConfig(testDir)).rejects.toThrow('missing required')
    })
  })

  describe('loadExtensionConfigSync', () => {
    it('should load a valid config file synchronously', async () => {
      const config = { name: 'sync-ext', version: '1.0.0' }
      await writeFile(join(testDir, CONFIG_FILENAME), JSON.stringify(config), 'utf-8')

      const loaded = loadExtensionConfigSync(testDir)
      expect(loaded.name).toBe('sync-ext')
    })
  })

  // =========================================================================
  // Install Metadata
  // =========================================================================

  describe('loadInstallMetadata', () => {
    it('should load install metadata', async () => {
      const metadata = { type: 'local', source: '/path/to/ext', installedAt: '2025-01-01' }
      await writeFile(join(testDir, INSTALL_METADATA_FILENAME), JSON.stringify(metadata), 'utf-8')

      const loaded = loadInstallMetadata(testDir)
      expect(loaded).toBeDefined()
      expect(loaded!.type).toBe('local')
      expect(loaded!.source).toBe('/path/to/ext')
    })

    it('should return undefined if metadata file missing', () => {
      const loaded = loadInstallMetadata(testDir)
      expect(loaded).toBeUndefined()
    })

    it('should return undefined if metadata is corrupted', async () => {
      await writeFile(join(testDir, INSTALL_METADATA_FILENAME), 'bad json', 'utf-8')
      const loaded = loadInstallMetadata(testDir)
      expect(loaded).toBeUndefined()
    })
  })

  // =========================================================================
  // Context Files
  // =========================================================================

  describe('getContextFilePaths', () => {
    it('should return default context files that exist', async () => {
      await writeFile(join(testDir, 'ESTELA.md'), '# Context', 'utf-8')

      const paths = getContextFilePaths({ name: 'test', version: '1.0.0' }, testDir)
      expect(paths).toHaveLength(1)
      expect(paths[0]).toContain('ESTELA.md')
    })

    it('should return empty array if no context files exist', () => {
      const paths = getContextFilePaths({ name: 'test', version: '1.0.0' }, testDir)
      expect(paths).toHaveLength(0)
    })

    it('should use custom context file names', async () => {
      await writeFile(join(testDir, 'CUSTOM.md'), '# Custom', 'utf-8')

      const paths = getContextFilePaths(
        { name: 'test', version: '1.0.0', contextFiles: ['CUSTOM.md'] },
        testDir
      )
      expect(paths).toHaveLength(1)
      expect(paths[0]).toContain('CUSTOM.md')
    })

    it('should handle string contextFiles config', async () => {
      await writeFile(join(testDir, 'SINGLE.md'), '# Single', 'utf-8')

      const paths = getContextFilePaths(
        { name: 'test', version: '1.0.0', contextFiles: 'SINGLE.md' },
        testDir
      )
      expect(paths).toHaveLength(1)
    })

    it('should filter out non-existent context files', async () => {
      await writeFile(join(testDir, 'EXISTS.md'), '# Exists', 'utf-8')

      const paths = getContextFilePaths(
        { name: 'test', version: '1.0.0', contextFiles: ['EXISTS.md', 'MISSING.md'] },
        testDir
      )
      expect(paths).toHaveLength(1)
      expect(paths[0]).toContain('EXISTS.md')
    })
  })

  // =========================================================================
  // Constants
  // =========================================================================

  describe('constants', () => {
    it('should have correct config filename', () => {
      expect(CONFIG_FILENAME).toBe('estela-extension.json')
    })

    it('should have correct default context files', () => {
      expect(DEFAULT_CONTEXT_FILES).toEqual(['ESTELA.md'])
    })
  })
})
