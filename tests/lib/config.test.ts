/**
 * Tests for Delta9 Configuration Loader
 */

import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import {
  loadConfig,
  getConfig,
  clearConfigCache,
  reloadConfig,
  isCouncilEnabled,
  getEnabledOracles,
  isBudgetEnabled,
  getBudgetLimit,
  getCommanderConfig,
  getCouncilConfig,
} from '../../src/lib/config.js'
import { DEFAULT_CONFIG } from '../../src/types/config.js'
import * as fs from 'node:fs'
import * as paths from '../../src/lib/paths.js'

// Mock file system and paths
vi.mock('node:fs', () => ({
  readFileSync: vi.fn(),
  existsSync: vi.fn(() => false),
}))

vi.mock('../../src/lib/paths.js', () => ({
  getGlobalConfigPath: vi.fn(() => '/home/user/.config/opencode/delta9.json'),
  getProjectConfigPath: vi.fn((cwd: string) => `${cwd}/.delta9/config.json`),
  globalConfigExists: vi.fn(() => false),
  projectConfigExists: vi.fn(() => false),
}))

describe('Config', () => {
  const testCwd = '/test/project'

  beforeEach(() => {
    vi.clearAllMocks()
    clearConfigCache()
  })

  afterEach(() => {
    vi.resetAllMocks()
  })

  describe('loadConfig', () => {
    it('returns default config when no config files exist', () => {
      vi.mocked(paths.globalConfigExists).mockReturnValue(false)
      vi.mocked(paths.projectConfigExists).mockReturnValue(false)

      const config = loadConfig(testCwd)

      expect(config).toEqual(DEFAULT_CONFIG)
    })

    it('merges global config with defaults', () => {
      vi.mocked(paths.globalConfigExists).mockReturnValue(true)
      vi.mocked(fs.existsSync).mockReturnValue(true)
      vi.mocked(fs.readFileSync).mockReturnValue(
        JSON.stringify({
          commander: { model: 'custom-model' },
        })
      )

      const config = loadConfig(testCwd, { validate: false })

      expect(config.commander.model).toBe('custom-model')
      // Other defaults preserved
      expect(config.council).toBeDefined()
    })

    it('project config overrides global config', () => {
      vi.mocked(paths.globalConfigExists).mockReturnValue(true)
      vi.mocked(paths.projectConfigExists).mockReturnValue(true)
      vi.mocked(fs.existsSync).mockReturnValue(true)
      vi.mocked(fs.readFileSync)
        .mockReturnValueOnce(JSON.stringify({ commander: { model: 'global-model' } }))
        .mockReturnValueOnce(JSON.stringify({ commander: { model: 'project-model' } }))

      const config = loadConfig(testCwd, { validate: false })

      expect(config.commander.model).toBe('project-model')
    })

    it('caches config by cwd', () => {
      const config1 = loadConfig(testCwd)
      const config2 = loadConfig(testCwd)

      expect(config1).toBe(config2)
    })

    it('returns different config for different cwd', () => {
      vi.mocked(paths.projectConfigExists).mockImplementation((cwd) => cwd === '/other')
      vi.mocked(fs.existsSync).mockReturnValue(true)
      vi.mocked(fs.readFileSync).mockReturnValue(
        JSON.stringify({ commander: { model: 'other-model' } })
      )

      const config1 = loadConfig(testCwd, { useCache: false, validate: false })
      clearConfigCache()
      const config2 = loadConfig('/other', { useCache: false, validate: false })

      expect(config1.commander.model).not.toBe(config2.commander.model)
    })

    it('bypasses cache when useCache is false', () => {
      const config1 = loadConfig(testCwd, { useCache: false })
      const config2 = loadConfig(testCwd, { useCache: false })

      // Should create new objects
      expect(config1).not.toBe(config2)
      // But with same values
      expect(config1).toEqual(config2)
    })

    it('handles JSON parse errors gracefully', () => {
      vi.mocked(paths.globalConfigExists).mockReturnValue(true)
      vi.mocked(fs.existsSync).mockReturnValue(true)
      vi.mocked(fs.readFileSync).mockReturnValue('invalid json')

      const consoleSpy = vi.spyOn(console, 'error').mockImplementation(() => {})
      const config = loadConfig(testCwd, { validate: false })

      expect(config).toBeDefined()
      consoleSpy.mockRestore()
    })
  })

  describe('getConfig', () => {
    it('returns cached config', () => {
      loadConfig(testCwd)
      const config = getConfig()

      expect(config).toEqual(DEFAULT_CONFIG)
    })

    it('returns defaults when not loaded', () => {
      const config = getConfig()
      expect(config).toEqual(DEFAULT_CONFIG)
    })
  })

  describe('clearConfigCache', () => {
    it('clears cached config', () => {
      const config1 = loadConfig(testCwd)
      clearConfigCache()
      const config2 = loadConfig(testCwd)

      expect(config1).not.toBe(config2)
    })
  })

  describe('reloadConfig', () => {
    it('clears cache and reloads', () => {
      loadConfig(testCwd)
      const config = reloadConfig(testCwd)

      expect(config).toBeDefined()
    })
  })

  describe('isCouncilEnabled', () => {
    it('returns council enabled status', () => {
      const enabled = isCouncilEnabled(testCwd)
      expect(typeof enabled).toBe('boolean')
    })
  })

  describe('getEnabledOracles', () => {
    it('filters to only enabled oracles from default config', () => {
      clearConfigCache()
      const oracles = getEnabledOracles(testCwd)

      // Should return only oracles where enabled=true from DEFAULT_CONFIG
      expect(oracles.every((o) => o.enabled)).toBe(true)
    })

    it('returns array of oracle configs', () => {
      const oracles = getEnabledOracles(testCwd)
      expect(Array.isArray(oracles)).toBe(true)
    })
  })

  describe('isBudgetEnabled', () => {
    it('returns budget enabled status', () => {
      const enabled = isBudgetEnabled(testCwd)
      expect(typeof enabled).toBe('boolean')
    })
  })

  describe('getBudgetLimit', () => {
    it('returns budget limit', () => {
      const limit = getBudgetLimit(testCwd)
      expect(typeof limit).toBe('number')
      expect(limit).toBeGreaterThan(0)
    })
  })

  describe('getCommanderConfig', () => {
    it('returns commander config section', () => {
      const commander = getCommanderConfig(testCwd)

      expect(commander).toBeDefined()
      expect(commander.model).toBeDefined()
    })
  })

  describe('getCouncilConfig', () => {
    it('returns council config section', () => {
      const council = getCouncilConfig(testCwd)

      expect(council).toBeDefined()
      expect(council.enabled).toBeDefined()
      expect(council.members).toBeDefined()
    })
  })
})
