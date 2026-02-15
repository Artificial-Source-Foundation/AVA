/**
 * Settings Export/Import
 *
 * Utilities for exporting and importing settings as JSON.
 * API keys are intentionally excluded from exports for security.
 */

import { getPlatform } from '../platform.js'
import type { SettingsManager } from './manager.js'
import { ExportableSettingsSchema } from './schema.js'
import type { ExportableSettings, Settings } from './types.js'
import { DEFAULT_SETTINGS, SETTINGS_VERSION } from './types.js'

// ============================================================================
// Export
// ============================================================================

/**
 * Export settings to JSON string
 * Does not include API keys
 */
export function exportSettingsToJson(settings: Settings): string {
  const exportable: ExportableSettings = {
    version: SETTINGS_VERSION,
    exportedAt: new Date().toISOString(),
    settings,
  }

  return JSON.stringify(exportable, null, 2)
}

/**
 * Export settings to file
 */
export async function exportSettingsToFile(settings: Settings, filePath: string): Promise<void> {
  const fs = getPlatform().fs
  const json = exportSettingsToJson(settings)
  await fs.writeFile(filePath, json)
}

/**
 * Export current settings from manager
 */
export async function exportFromManager(manager: SettingsManager, filePath: string): Promise<void> {
  const settings = manager.getAll()
  await exportSettingsToFile(settings, filePath)
}

// ============================================================================
// Import
// ============================================================================

/**
 * Result of import operation
 */
export interface ImportResult {
  success: boolean
  settings?: Settings
  errors: string[]
  warnings: string[]
}

/**
 * Import settings from JSON string
 */
export function importSettingsFromJson(json: string): ImportResult {
  const result: ImportResult = {
    success: false,
    errors: [],
    warnings: [],
  }

  try {
    const parsed = JSON.parse(json)

    // Validate structure
    const validation = ExportableSettingsSchema.safeParse(parsed)
    if (!validation.success) {
      result.errors = validation.error.issues.map((i) => `${i.path.join('.')}: ${i.message}`)
      return result
    }

    const exportable = validation.data

    // Check version
    if (exportable.version > SETTINGS_VERSION) {
      result.warnings.push(
        `Settings were exported from a newer version (v${exportable.version}). Some settings may not be recognized.`
      )
    }

    if (exportable.version < SETTINGS_VERSION) {
      result.warnings.push(
        `Settings were exported from an older version (v${exportable.version}). Missing settings will use defaults.`
      )
    }

    result.settings = exportable.settings as Settings
    result.success = true
  } catch (err) {
    result.errors.push(`Invalid JSON: ${err instanceof Error ? err.message : String(err)}`)
  }

  return result
}

/**
 * Import settings from file
 */
export async function importSettingsFromFile(filePath: string): Promise<ImportResult> {
  const fs = getPlatform().fs

  try {
    if (!(await fs.exists(filePath))) {
      return {
        success: false,
        errors: [`File not found: ${filePath}`],
        warnings: [],
      }
    }

    const json = await fs.readFile(filePath)
    return importSettingsFromJson(json)
  } catch (err) {
    return {
      success: false,
      errors: [`Failed to read file: ${err instanceof Error ? err.message : String(err)}`],
      warnings: [],
    }
  }
}

/**
 * Import settings into manager
 */
export async function importToManager(
  manager: SettingsManager,
  filePath: string
): Promise<ImportResult> {
  const result = await importSettingsFromFile(filePath)

  if (result.success && result.settings) {
    // Apply each category
    for (const [category, value] of Object.entries(result.settings)) {
      manager.set(category as keyof Settings, value as Settings[keyof Settings])
    }

    // Save to storage
    await manager.save()
  }

  return result
}

// ============================================================================
// Merge Utilities
// ============================================================================

/**
 * Merge imported settings with existing
 * Only updates fields that are explicitly set in the import
 */
export function mergeSettings(existing: Settings, imported: Partial<Settings>): Settings {
  const merged: Settings = structuredClone(existing)

  for (const category of Object.keys(imported) as (keyof Settings)[]) {
    if (imported[category] !== undefined) {
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      // biome-ignore lint/suspicious/noExplicitAny: TypeScript generic inference limitation
      ;(merged as any)[category] = {
        ...merged[category],
        ...imported[category],
      }
    }
  }

  return merged
}

/**
 * Get diff between two settings
 */
export interface SettingsDiff {
  category: keyof Settings
  field: string
  oldValue: unknown
  newValue: unknown
}

export function diffSettings(oldSettings: Settings, newSettings: Settings): SettingsDiff[] {
  const diffs: SettingsDiff[] = []

  for (const category of Object.keys(oldSettings) as (keyof Settings)[]) {
    const oldCategory = oldSettings[category] as unknown as Record<string, unknown>
    const newCategory = newSettings[category] as unknown as Record<string, unknown>

    for (const field of new Set([...Object.keys(oldCategory), ...Object.keys(newCategory)])) {
      const oldValue = oldCategory[field]
      const newValue = newCategory[field]

      if (JSON.stringify(oldValue) !== JSON.stringify(newValue)) {
        diffs.push({ category, field, oldValue, newValue })
      }
    }
  }

  return diffs
}

// ============================================================================
// Preview Utilities
// ============================================================================

/**
 * Preview what will change when importing settings
 */
export async function previewImport(
  manager: SettingsManager,
  filePath: string
): Promise<{ result: ImportResult; diffs: SettingsDiff[] }> {
  const result = await importSettingsFromFile(filePath)

  if (!result.success || !result.settings) {
    return { result, diffs: [] }
  }

  const current = manager.getAll()
  const diffs = diffSettings(current, result.settings)

  return { result, diffs }
}

// ============================================================================
// Reset Utilities
// ============================================================================

/**
 * Create a backup of current settings before reset
 */
export async function backupSettings(manager: SettingsManager): Promise<string> {
  const timestamp = new Date().toISOString().replace(/[:.]/g, '-')
  const backupPath = `~/.ava/settings-backup-${timestamp}.json`

  await exportFromManager(manager, backupPath)
  return backupPath
}

/**
 * Reset settings to defaults
 */
export async function resetToDefaults(
  manager: SettingsManager,
  createBackup = true
): Promise<string | null> {
  let backupPath: string | null = null

  if (createBackup && manager.isDirty()) {
    backupPath = await backupSettings(manager)
  }

  manager.resetAll()
  await manager.save()

  return backupPath
}

/**
 * Export default settings (for reference)
 */
export function getDefaultSettingsJson(): string {
  return exportSettingsToJson(DEFAULT_SETTINGS)
}
