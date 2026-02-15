/**
 * Settings Export/Import Tests
 */

import { describe, expect, it } from 'vitest'
import {
  diffSettings,
  exportSettingsToJson,
  getDefaultSettingsJson,
  importSettingsFromJson,
  mergeSettings,
} from './export.js'
import type { Settings } from './types.js'
import { DEFAULT_SETTINGS, SETTINGS_VERSION } from './types.js'

// ============================================================================
// Export
// ============================================================================

describe('exportSettingsToJson', () => {
  it('returns valid JSON string', () => {
    const json = exportSettingsToJson(DEFAULT_SETTINGS)
    const parsed = JSON.parse(json)
    expect(parsed).toBeDefined()
  })

  it('includes version', () => {
    const json = exportSettingsToJson(DEFAULT_SETTINGS)
    const parsed = JSON.parse(json)
    expect(parsed.version).toBe(SETTINGS_VERSION)
  })

  it('includes exportedAt timestamp', () => {
    const json = exportSettingsToJson(DEFAULT_SETTINGS)
    const parsed = JSON.parse(json)
    expect(parsed.exportedAt).toBeTruthy()
    expect(new Date(parsed.exportedAt).getTime()).not.toBeNaN()
  })

  it('includes settings', () => {
    const json = exportSettingsToJson(DEFAULT_SETTINGS)
    const parsed = JSON.parse(json)
    expect(parsed.settings.provider.defaultProvider).toBe('anthropic')
  })

  it('pretty-prints with 2-space indent', () => {
    const json = exportSettingsToJson(DEFAULT_SETTINGS)
    expect(json).toContain('\n  ')
  })
})

// ============================================================================
// Import
// ============================================================================

describe('importSettingsFromJson', () => {
  it('imports valid exported settings', () => {
    const json = exportSettingsToJson(DEFAULT_SETTINGS)
    const result = importSettingsFromJson(json)
    expect(result.success).toBe(true)
    expect(result.settings).toBeDefined()
    expect(result.errors).toHaveLength(0)
  })

  it('rejects invalid JSON', () => {
    const result = importSettingsFromJson('not json')
    expect(result.success).toBe(false)
    expect(result.errors.length).toBeGreaterThan(0)
  })

  it('rejects invalid structure', () => {
    const result = importSettingsFromJson('{"foo": "bar"}')
    expect(result.success).toBe(false)
    expect(result.errors.length).toBeGreaterThan(0)
  })

  it('warns about newer version', () => {
    const exported = {
      version: SETTINGS_VERSION + 1,
      exportedAt: new Date().toISOString(),
      settings: DEFAULT_SETTINGS,
    }
    const result = importSettingsFromJson(JSON.stringify(exported))
    expect(result.success).toBe(true)
    expect(result.warnings.length).toBeGreaterThan(0)
    expect(result.warnings[0]).toContain('newer version')
  })

  it('warns about older version when version < current', () => {
    // Only test if SETTINGS_VERSION > 1
    if (SETTINGS_VERSION > 1) {
      const exported = {
        version: SETTINGS_VERSION - 1,
        exportedAt: new Date().toISOString(),
        settings: DEFAULT_SETTINGS,
      }
      const result = importSettingsFromJson(JSON.stringify(exported))
      expect(result.success).toBe(true)
      expect(result.warnings.some((w: string) => w.includes('older version'))).toBe(true)
    }
  })

  it('round-trips settings', () => {
    const json = exportSettingsToJson(DEFAULT_SETTINGS)
    const result = importSettingsFromJson(json)
    expect(result.settings?.provider.defaultProvider).toBe(
      DEFAULT_SETTINGS.provider.defaultProvider
    )
    expect(result.settings?.agent.maxTurns).toBe(DEFAULT_SETTINGS.agent.maxTurns)
    expect(result.settings?.ui.theme).toBe(DEFAULT_SETTINGS.ui.theme)
  })
})

// ============================================================================
// Merge
// ============================================================================

describe('mergeSettings', () => {
  it('returns existing when imported is empty', () => {
    const merged = mergeSettings(DEFAULT_SETTINGS, {} as Partial<Settings>)
    expect(merged).toEqual(DEFAULT_SETTINGS)
  })

  it('overrides specific category', () => {
    const imported: Partial<Settings> = {
      ui: { ...DEFAULT_SETTINGS.ui, theme: 'dark' },
    }
    const merged = mergeSettings(DEFAULT_SETTINGS, imported)
    expect(merged.ui.theme).toBe('dark')
    expect(merged.provider).toEqual(DEFAULT_SETTINGS.provider)
  })

  it('preserves fields not in imported category', () => {
    const imported: Partial<Settings> = {
      ui: { theme: 'light' } as Settings['ui'],
    }
    const merged = mergeSettings(DEFAULT_SETTINGS, imported)
    // Spread merges, so existing fields preserved
    expect(merged.ui.fontSize).toBe(DEFAULT_SETTINGS.ui.fontSize)
  })

  it('does not mutate existing settings', () => {
    const existing = structuredClone(DEFAULT_SETTINGS)
    const imported: Partial<Settings> = {
      ui: { ...DEFAULT_SETTINGS.ui, theme: 'dark' },
    }
    mergeSettings(existing, imported)
    expect(existing.ui.theme).toBe(DEFAULT_SETTINGS.ui.theme)
  })
})

// ============================================================================
// Diff
// ============================================================================

describe('diffSettings', () => {
  it('returns empty for identical settings', () => {
    const diffs = diffSettings(DEFAULT_SETTINGS, structuredClone(DEFAULT_SETTINGS))
    expect(diffs).toHaveLength(0)
  })

  it('detects changed fields', () => {
    const modified = structuredClone(DEFAULT_SETTINGS)
    modified.ui.theme = 'dark'
    const diffs = diffSettings(DEFAULT_SETTINGS, modified)
    expect(diffs).toHaveLength(1)
    expect(diffs[0].category).toBe('ui')
    expect(diffs[0].field).toBe('theme')
    expect(diffs[0].oldValue).toBe('system')
    expect(diffs[0].newValue).toBe('dark')
  })

  it('detects multiple changes across categories', () => {
    const modified = structuredClone(DEFAULT_SETTINGS)
    modified.ui.fontSize = 20
    modified.agent.maxTurns = 100
    const diffs = diffSettings(DEFAULT_SETTINGS, modified)
    expect(diffs).toHaveLength(2)
  })
})

// ============================================================================
// Default Settings JSON
// ============================================================================

describe('getDefaultSettingsJson', () => {
  it('returns valid JSON', () => {
    const json = getDefaultSettingsJson()
    const parsed = JSON.parse(json)
    expect(parsed.version).toBe(SETTINGS_VERSION)
    expect(parsed.settings).toBeDefined()
  })

  it('contains default provider', () => {
    const json = getDefaultSettingsJson()
    expect(json).toContain('anthropic')
  })
})
