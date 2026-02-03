/**
 * Settings Migration
 *
 * Utilities for migrating settings and credentials between versions.
 * Handles backwards compatibility and format upgrades.
 */

import { SettingsSchema } from './schema.js'
import type { Settings, SettingsCategory } from './types.js'
import { DEFAULT_SETTINGS, SETTINGS_VERSION } from './types.js'

// ============================================================================
// Version Migration
// ============================================================================

/** Migration function signature */
type MigrationFn = (settings: Record<string, unknown>) => Record<string, unknown>

/** Version migrations (from version N to N+1) */
const VERSION_MIGRATIONS: Record<number, MigrationFn> = {
  // Example: Migration from version 1 to 2
  // 1: (settings) => {
  //   // Add new field with default value
  //   return {
  //     ...settings,
  //     newCategory: { newField: 'default' }
  //   }
  // }
}

/**
 * Migrate settings from an older version to current
 */
export function migrateSettings(settings: Record<string, unknown>, fromVersion: number): Settings {
  let current = { ...settings }
  let version = fromVersion

  // Apply migrations in order
  while (version < SETTINGS_VERSION) {
    const migration = VERSION_MIGRATIONS[version]
    if (migration) {
      current = migration(current)
    }
    version++
  }

  // Validate and fill missing with defaults
  const result = SettingsSchema.safeParse(current)
  if (result.success) {
    return result.data as Settings
  }

  // If validation fails, merge with defaults
  return mergeWithDefaults(current)
}

/**
 * Merge partial settings with defaults
 */
export function mergeWithDefaults(partial: Record<string, unknown>): Settings {
  const merged: Settings = structuredClone(DEFAULT_SETTINGS)

  for (const key of Object.keys(partial) as SettingsCategory[]) {
    if (partial[key] !== undefined && merged[key] !== undefined) {
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      // biome-ignore lint/suspicious/noExplicitAny: TypeScript generic inference limitation
      ;(merged as any)[key] = {
        ...merged[key],
        ...(partial[key] as object),
      }
    }
  }

  return merged
}

// ============================================================================
// Environment Variable Migration
// ============================================================================

/** Environment variable to provider mapping */
const ENV_KEY_MAPPING: Record<string, { provider: string; envVar: string }> = {
  ANTHROPIC_API_KEY: { provider: 'anthropic', envVar: 'ANTHROPIC_API_KEY' },
  OPENAI_API_KEY: { provider: 'openai', envVar: 'OPENAI_API_KEY' },
  OPENROUTER_API_KEY: { provider: 'openrouter', envVar: 'OPENROUTER_API_KEY' },
  GOOGLE_AI_API_KEY: { provider: 'google', envVar: 'GOOGLE_AI_API_KEY' },
  COHERE_API_KEY: { provider: 'cohere', envVar: 'COHERE_API_KEY' },
  MISTRAL_API_KEY: { provider: 'mistral', envVar: 'MISTRAL_API_KEY' },
}

/**
 * Find API keys in environment variables
 * Returns map of provider -> key
 */
export function findEnvApiKeys(): Map<string, string> {
  const found = new Map<string, string>()

  for (const [envName, { provider }] of Object.entries(ENV_KEY_MAPPING)) {
    const value = process.env[envName]
    if (value && value.length > 0) {
      found.set(provider, value)
    }
  }

  return found
}

/**
 * Get migration report for environment API keys
 */
export interface EnvMigrationReport {
  found: { provider: string; envVar: string }[]
  missing: { provider: string; envVar: string }[]
}

export function getEnvMigrationReport(): EnvMigrationReport {
  const report: EnvMigrationReport = { found: [], missing: [] }

  for (const [envName, info] of Object.entries(ENV_KEY_MAPPING)) {
    const value = process.env[envName]
    if (value && value.length > 0) {
      report.found.push(info)
    } else {
      report.missing.push(info)
    }
  }

  return report
}

// ============================================================================
// Legacy Settings Migration
// ============================================================================

/** Legacy settings file paths to check */
const LEGACY_PATHS = [
  '~/.config/estela/settings.json',
  '~/.estela-settings.json',
  '.estela/settings.json',
]

/**
 * Check for legacy settings files
 */
export function getLegacyPaths(): string[] {
  return LEGACY_PATHS
}

/**
 * Legacy settings format (if different from current)
 */
export interface LegacySettings {
  version?: number
  [key: string]: unknown
}

/**
 * Check if settings need migration based on version
 */
export function needsMigration(settings: LegacySettings): boolean {
  const version = settings.version ?? 0
  return version < SETTINGS_VERSION
}

/**
 * Get current settings version
 */
export function getCurrentVersion(): number {
  return SETTINGS_VERSION
}

// ============================================================================
// Import/Export Utilities
// ============================================================================

/**
 * Validate imported settings structure
 */
export function validateImportedSettings(data: unknown): data is Settings {
  const result = SettingsSchema.safeParse(data)
  return result.success
}

/**
 * Sanitize settings for export (remove sensitive data)
 */
export function sanitizeForExport(settings: Settings): Settings {
  // Settings don't contain API keys (those are in credentials)
  // Just return a deep clone
  return structuredClone(settings)
}

/**
 * Get list of fields that changed between two settings objects
 */
export function getChangedFields(
  oldSettings: Settings,
  newSettings: Settings
): { category: SettingsCategory; field: string; oldValue: unknown; newValue: unknown }[] {
  const changes: {
    category: SettingsCategory
    field: string
    oldValue: unknown
    newValue: unknown
  }[] = []

  for (const category of Object.keys(oldSettings) as SettingsCategory[]) {
    const oldCategory = oldSettings[category] as unknown as Record<string, unknown>
    const newCategory = newSettings[category] as unknown as Record<string, unknown>

    for (const field of Object.keys(oldCategory)) {
      if (JSON.stringify(oldCategory[field]) !== JSON.stringify(newCategory[field])) {
        changes.push({
          category,
          field,
          oldValue: oldCategory[field],
          newValue: newCategory[field],
        })
      }
    }
  }

  return changes
}
