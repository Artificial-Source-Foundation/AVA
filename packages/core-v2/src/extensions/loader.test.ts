/**
 * Extension loader — discovers and imports extension modules.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { installMockPlatform, type MockPlatform } from '../__test-utils__/mock-platform.js'
import { resetLogger } from '../logger/logger.js'
import {
  loadAllBuiltInExtensions,
  loadBuiltInExtension,
  loadExtensionsFromDirectory,
} from './loader.js'
import type { ExtensionManifest, ExtensionModule } from './types.js'

let platform: MockPlatform

beforeEach(() => {
  platform = installMockPlatform()
})

afterEach(() => {
  resetLogger()
  vi.restoreAllMocks()
})

describe('loadExtensionsFromDirectory', () => {
  it('returns empty array when directory does not exist', async () => {
    const result = await loadExtensionsFromDirectory('/nonexistent')
    expect(result).toEqual([])
  })

  it('returns empty array when directory has no subdirectories', async () => {
    platform.fs.addDir('/extensions')
    platform.fs.addFile('/extensions/readme.txt', 'hello')
    const result = await loadExtensionsFromDirectory('/extensions')
    expect(result).toEqual([])
  })

  it('skips subdirectory without manifest', async () => {
    platform.fs.addDir('/extensions')
    platform.fs.addDir('/extensions/my-ext')
    platform.fs.addFile('/extensions/my-ext/index.js', 'export function activate() {}')
    const result = await loadExtensionsFromDirectory('/extensions')
    expect(result).toEqual([])
  })

  it('skips manifest missing name', async () => {
    platform.fs.addDir('/extensions')
    platform.fs.addDir('/extensions/my-ext')
    platform.fs.addFile(
      '/extensions/my-ext/ava-extension.json',
      JSON.stringify({ version: '1.0', main: 'index.js' })
    )
    const result = await loadExtensionsFromDirectory('/extensions')
    expect(result).toEqual([])
  })

  it('skips manifest missing main', async () => {
    platform.fs.addDir('/extensions')
    platform.fs.addDir('/extensions/my-ext')
    platform.fs.addFile(
      '/extensions/my-ext/ava-extension.json',
      JSON.stringify({ name: 'my-ext', version: '1.0' })
    )
    const result = await loadExtensionsFromDirectory('/extensions')
    expect(result).toEqual([])
  })

  it('skips module without activate function (import fails → caught)', async () => {
    platform.fs.addDir('/extensions')
    platform.fs.addDir('/extensions/my-ext')
    platform.fs.addFile(
      '/extensions/my-ext/ava-extension.json',
      JSON.stringify({ name: 'my-ext', version: '1.0', main: 'index.js' })
    )
    // The dynamic import will fail since the file doesn't exist on real disk,
    // which exercises the error catch path
    const result = await loadExtensionsFromDirectory('/extensions')
    expect(result).toEqual([])
  })

  it('handles invalid JSON manifest gracefully', async () => {
    platform.fs.addDir('/extensions')
    platform.fs.addDir('/extensions/my-ext')
    platform.fs.addFile('/extensions/my-ext/ava-extension.json', 'not-json{{{')
    const result = await loadExtensionsFromDirectory('/extensions')
    expect(result).toEqual([])
  })

  it('handles import errors gracefully', async () => {
    platform.fs.addDir('/extensions')
    platform.fs.addDir('/extensions/my-ext')
    platform.fs.addFile(
      '/extensions/my-ext/ava-extension.json',
      JSON.stringify({ name: 'my-ext', version: '1.0', main: 'index.js' })
    )
    // The dynamic import will fail since the file doesn't really exist on disk
    const result = await loadExtensionsFromDirectory('/extensions')
    expect(result).toEqual([])
  })
})

describe('loadBuiltInExtension', () => {
  it('returns manifest with builtIn=true', () => {
    const manifest: ExtensionManifest = {
      name: 'test-ext',
      version: '1.0.0',
      main: 'index.js',
    }
    const module: ExtensionModule = {
      activate: () => undefined,
    }
    const result = loadBuiltInExtension(manifest, module)
    expect(result.manifest.builtIn).toBe(true)
    expect(result.manifest.name).toBe('test-ext')
  })

  it('sets path to <built-in>', () => {
    const manifest: ExtensionManifest = {
      name: 'built-in-ext',
      version: '2.0.0',
      main: 'index.js',
    }
    const module: ExtensionModule = {
      activate: () => undefined,
    }
    const result = loadBuiltInExtension(manifest, module)
    expect(result.path).toBe('<built-in>')
  })

  it('preserves original manifest fields', () => {
    const manifest: ExtensionManifest = {
      name: 'test-ext',
      version: '1.0.0',
      description: 'A test extension',
      main: 'index.js',
      priority: 10,
    }
    const module: ExtensionModule = {
      activate: () => undefined,
    }
    const result = loadBuiltInExtension(manifest, module)
    expect(result.manifest.description).toBe('A test extension')
    expect(result.manifest.priority).toBe(10)
    expect(result.manifest.version).toBe('1.0.0')
  })

  it('does not mutate the original manifest', () => {
    const manifest: ExtensionManifest = {
      name: 'test-ext',
      version: '1.0.0',
      main: 'index.js',
    }
    const module: ExtensionModule = {
      activate: () => undefined,
    }
    loadBuiltInExtension(manifest, module)
    expect(manifest.builtIn).toBeUndefined()
  })

  it('returns the module reference', () => {
    const manifest: ExtensionManifest = {
      name: 'test-ext',
      version: '1.0.0',
      main: 'index.js',
    }
    const activateFn = vi.fn()
    const module: ExtensionModule = { activate: activateFn }
    const result = loadBuiltInExtension(manifest, module)
    expect(result.module).toBe(module)
    expect(result.module.activate).toBe(activateFn)
  })
})

describe('loadAllBuiltInExtensions', () => {
  it('returns empty array when directory does not exist', async () => {
    const result = await loadAllBuiltInExtensions('/nonexistent')
    expect(result).toEqual([])
  })

  it('combines top-level and providers/* extensions', async () => {
    const topLevelExt = {
      manifest: { name: 'permissions', version: '1.0.0', main: 'index.js' },
      module: { activate: vi.fn() },
      path: '/extensions/permissions',
    }
    const providerExt = {
      manifest: { name: 'anthropic', version: '1.0.0', main: 'index.js' },
      module: { activate: vi.fn() },
      path: '/extensions/providers/anthropic',
    }

    // Mock loader: first call returns top-level, second returns providers
    const mockLoader = vi.fn()
    mockLoader.mockResolvedValueOnce([topLevelExt])
    mockLoader.mockResolvedValueOnce([providerExt])

    platform.fs.addDir('/extensions')

    const result = await loadAllBuiltInExtensions('/extensions', mockLoader)

    expect(result).toHaveLength(2)
    expect(result[0]!.manifest.name).toBe('permissions')
    expect(result[1]!.manifest.name).toBe('anthropic')

    // Verify loader was called for top-level and providers/
    expect(mockLoader).toHaveBeenCalledTimes(2)
    expect(mockLoader).toHaveBeenCalledWith('/extensions')
    expect(mockLoader).toHaveBeenCalledWith('/extensions/providers')
  })

  it('marks all loaded extensions as builtIn', async () => {
    const ext = {
      manifest: { name: 'test', version: '1.0.0', main: 'index.js' },
      module: { activate: vi.fn() },
      path: '/extensions/test',
    }

    const mockLoader = vi.fn()
    mockLoader.mockResolvedValueOnce([ext])
    mockLoader.mockResolvedValueOnce([])

    platform.fs.addDir('/extensions')

    const result = await loadAllBuiltInExtensions('/extensions', mockLoader)

    expect(result).toHaveLength(1)
    expect(result[0]!.manifest.builtIn).toBe(true)
  })

  it('handles missing providers directory gracefully', async () => {
    const ext = {
      manifest: { name: 'hooks', version: '1.0.0', main: 'index.js' },
      module: { activate: vi.fn() },
      path: '/extensions/hooks',
    }

    const mockLoader = vi.fn()
    // Top-level returns one, providers returns empty (dir doesn't exist)
    mockLoader.mockResolvedValueOnce([ext])
    mockLoader.mockResolvedValueOnce([])

    platform.fs.addDir('/extensions')

    const result = await loadAllBuiltInExtensions('/extensions', mockLoader)

    expect(result).toHaveLength(1)
    expect(result[0]!.manifest.name).toBe('hooks')
  })
})
