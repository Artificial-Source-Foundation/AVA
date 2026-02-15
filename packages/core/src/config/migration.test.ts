/**
 * Settings Migration Tests
 */

import { describe, expect, it } from 'vitest'
import {
  findEnvApiKeys,
  getChangedFields,
  getCurrentVersion,
  getEnvMigrationReport,
  getLegacyPaths,
  mergeWithDefaults,
  migrateSettings,
  needsMigration,
  sanitizeForExport,
  validateImportedSettings,
} from './migration.js'
import { DEFAULT_SETTINGS, SETTINGS_VERSION } from './types.js'

// ============================================================================
// Version Migration
// ============================================================================

describe('migrateSettings', () => {
  it('returns valid settings for current version', () => {
    const result = migrateSettings(
      DEFAULT_SETTINGS as unknown as Record<string, unknown>,
      SETTINGS_VERSION
    )
    expect(result.provider.defaultProvider).toBe('anthropic')
  })

  it('fills missing fields with defaults', () => {
    const partial = { provider: { defaultProvider: 'openai' } }
    const result = migrateSettings(partial as Record<string, unknown>, SETTINGS_VERSION)
    expect(result.provider.defaultProvider).toBe('openai')
    expect(result.agent.maxTurns).toBe(DEFAULT_SETTINGS.agent.maxTurns)
  })

  it('applies migrations from older version', () => {
    const result = migrateSettings({ provider: {} } as Record<string, unknown>, 0)
    expect(result).toBeDefined()
    expect(result.provider).toBeDefined()
  })

  it('handles empty object gracefully', () => {
    const result = migrateSettings({}, 0)
    expect(result.provider.defaultProvider).toBe(DEFAULT_SETTINGS.provider.defaultProvider)
  })
})

// ============================================================================
// Merge With Defaults
// ============================================================================

describe('mergeWithDefaults', () => {
  it('returns defaults for empty partial', () => {
    const result = mergeWithDefaults({})
    expect(result).toEqual(DEFAULT_SETTINGS)
  })

  it('overrides specific fields', () => {
    const result = mergeWithDefaults({
      ui: { theme: 'dark' },
    } as Record<string, unknown>)
    expect(result.ui.theme).toBe('dark')
    expect(result.ui.fontSize).toBe(DEFAULT_SETTINGS.ui.fontSize)
  })

  it('preserves defaults for missing categories', () => {
    const result = mergeWithDefaults({ provider: { timeout: 5000 } } as Record<string, unknown>)
    expect(result.agent).toEqual(DEFAULT_SETTINGS.agent)
    expect(result.memory).toEqual(DEFAULT_SETTINGS.memory)
  })

  it('ignores unknown categories', () => {
    const result = mergeWithDefaults({ unknownCategory: { foo: 'bar' } } as Record<string, unknown>)
    expect(result.provider).toEqual(DEFAULT_SETTINGS.provider)
  })
})

// ============================================================================
// Environment Variable Migration
// ============================================================================

describe('findEnvApiKeys', () => {
  it('returns empty map when no env vars set', () => {
    const original = { ...process.env }
    delete process.env.ANTHROPIC_API_KEY
    delete process.env.OPENAI_API_KEY
    delete process.env.GOOGLE_AI_API_KEY
    const found = findEnvApiKeys()
    expect(found.size).toBe(0)
    Object.assign(process.env, original)
  })

  it('finds set env vars', () => {
    const original = process.env.ANTHROPIC_API_KEY
    process.env.ANTHROPIC_API_KEY = 'sk-ant-test123'
    const found = findEnvApiKeys()
    expect(found.has('anthropic')).toBe(true)
    expect(found.get('anthropic')).toBe('sk-ant-test123')
    if (original) {
      process.env.ANTHROPIC_API_KEY = original
    } else {
      delete process.env.ANTHROPIC_API_KEY
    }
  })

  it('ignores empty env vars', () => {
    const original = process.env.OPENAI_API_KEY
    process.env.OPENAI_API_KEY = ''
    const found = findEnvApiKeys()
    expect(found.has('openai')).toBe(false)
    if (original) {
      process.env.OPENAI_API_KEY = original
    } else {
      delete process.env.OPENAI_API_KEY
    }
  })
})

describe('getEnvMigrationReport', () => {
  it('returns found and missing lists', () => {
    const report = getEnvMigrationReport()
    expect(report.found).toBeDefined()
    expect(report.missing).toBeDefined()
    expect(report.found.length + report.missing.length).toBeGreaterThan(0)
  })

  it('each entry has provider and envVar', () => {
    const report = getEnvMigrationReport()
    for (const entry of [...report.found, ...report.missing]) {
      expect(entry.provider).toBeTruthy()
      expect(entry.envVar).toBeTruthy()
    }
  })
})

// ============================================================================
// Legacy Settings
// ============================================================================

describe('getLegacyPaths', () => {
  it('returns array of paths', () => {
    const paths = getLegacyPaths()
    expect(paths.length).toBeGreaterThan(0)
    for (const p of paths) {
      expect(typeof p).toBe('string')
    }
  })
})

describe('needsMigration', () => {
  it('returns true for version 0', () => {
    expect(needsMigration({ version: 0 })).toBe(true)
  })

  it('returns true when no version', () => {
    expect(needsMigration({})).toBe(true)
  })

  it('returns false for current version', () => {
    expect(needsMigration({ version: SETTINGS_VERSION })).toBe(false)
  })
})

describe('getCurrentVersion', () => {
  it('returns SETTINGS_VERSION', () => {
    expect(getCurrentVersion()).toBe(SETTINGS_VERSION)
  })
})

// ============================================================================
// Import/Export Utilities
// ============================================================================

describe('validateImportedSettings', () => {
  it('validates correct settings', () => {
    expect(validateImportedSettings(DEFAULT_SETTINGS)).toBe(true)
  })

  it('rejects invalid data', () => {
    expect(validateImportedSettings({})).toBe(false)
    expect(validateImportedSettings(null)).toBe(false)
    expect(validateImportedSettings('not-settings')).toBe(false)
  })
})

describe('sanitizeForExport', () => {
  it('returns deep clone', () => {
    const sanitized = sanitizeForExport(DEFAULT_SETTINGS)
    expect(sanitized).toEqual(DEFAULT_SETTINGS)
    expect(sanitized).not.toBe(DEFAULT_SETTINGS)
    expect(sanitized.provider).not.toBe(DEFAULT_SETTINGS.provider)
  })
})

// ============================================================================
// Changed Fields
// ============================================================================

describe('getChangedFields', () => {
  it('returns empty for identical settings', () => {
    const changes = getChangedFields(DEFAULT_SETTINGS, structuredClone(DEFAULT_SETTINGS))
    expect(changes).toHaveLength(0)
  })

  it('detects changed fields', () => {
    const modified = structuredClone(DEFAULT_SETTINGS)
    modified.ui.theme = 'dark'
    modified.agent.maxTurns = 100
    const changes = getChangedFields(DEFAULT_SETTINGS, modified)
    expect(changes).toHaveLength(2)
    expect(changes.find((c) => c.category === 'ui' && c.field === 'theme')).toBeDefined()
    expect(changes.find((c) => c.category === 'agent' && c.field === 'maxTurns')).toBeDefined()
  })

  it('includes old and new values', () => {
    const modified = structuredClone(DEFAULT_SETTINGS)
    modified.ui.fontSize = 18
    const changes = getChangedFields(DEFAULT_SETTINGS, modified)
    const fontChange = changes.find((c) => c.field === 'fontSize')
    expect(fontChange?.oldValue).toBe(14)
    expect(fontChange?.newValue).toBe(18)
  })
})
