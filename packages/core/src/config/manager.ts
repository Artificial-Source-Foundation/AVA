/**
 * Settings Manager
 *
 * Manages application settings with validation, persistence, and reactive updates.
 *
 * Usage:
 * ```ts
 * const manager = new SettingsManager()
 * await manager.load()
 *
 * // Get settings by category
 * const agentSettings = manager.get('agent')
 *
 * // Update settings
 * manager.set('agent', { maxTurns: 100 })
 *
 * // Listen for changes
 * const unsubscribe = manager.on((event) => {
 *   if (event.type === 'category_changed') {
 *     console.log(`${event.category} settings changed`)
 *   }
 * })
 * ```
 */

import type { ZodError, ZodSchema } from 'zod'
import {
  AgentSettingsSchema,
  ContextSettingsSchema,
  GitConfigSchema,
  MemorySettingsSchema,
  PartialAgentSettingsSchema,
  PartialContextSettingsSchema,
  PartialGitConfigSchema,
  PartialMemorySettingsSchema,
  PartialPermissionSettingsSchema,
  PartialProviderSettingsSchema,
  PartialUISettingsSchema,
  PermissionSettingsSchema,
  ProviderSettingsSchema,
  SettingsSchema,
  UISettingsSchema,
} from './schema.js'
import { loadSettingsFromFile, saveSettingsToFile } from './storage.js'
import type {
  AgentSettings,
  ContextSettings,
  MemorySettings,
  PermissionSettings,
  ProviderSettings,
  Settings,
  SettingsCategory,
  SettingsEvent,
  SettingsEventListener,
  UISettings,
} from './types.js'
import { DEFAULT_SETTINGS } from './types.js'

// ============================================================================
// Schema Mapping
// ============================================================================

/** Full schemas by category */
const CATEGORY_SCHEMAS: Record<SettingsCategory, ZodSchema> = {
  provider: ProviderSettingsSchema,
  agent: AgentSettingsSchema,
  permissions: PermissionSettingsSchema,
  context: ContextSettingsSchema,
  memory: MemorySettingsSchema,
  ui: UISettingsSchema,
  git: GitConfigSchema,
}

/** Partial schemas by category (for updates) */
const PARTIAL_SCHEMAS: Record<SettingsCategory, ZodSchema> = {
  provider: PartialProviderSettingsSchema,
  agent: PartialAgentSettingsSchema,
  permissions: PartialPermissionSettingsSchema,
  context: PartialContextSettingsSchema,
  memory: PartialMemorySettingsSchema,
  ui: PartialUISettingsSchema,
  git: PartialGitConfigSchema,
}

// ============================================================================
// Settings Manager
// ============================================================================

/**
 * Manages application settings with validation and persistence
 */
export class SettingsManager {
  private settings: Settings
  private listeners = new Set<SettingsEventListener>()
  private loaded = false
  private dirty = false

  constructor() {
    // Start with defaults
    this.settings = structuredClone(DEFAULT_SETTINGS)
  }

  // ==========================================================================
  // Load / Save
  // ==========================================================================

  /**
   * Load settings from storage
   * Merges with defaults for any missing values
   */
  async load(): Promise<void> {
    const stored = await loadSettingsFromFile()

    if (stored) {
      // Deep merge stored settings with defaults
      this.settings = this.mergeSettings(DEFAULT_SETTINGS, stored)

      // Validate the merged settings
      const result = SettingsSchema.safeParse(this.settings)
      if (!result.success) {
        console.warn('Invalid stored settings, using defaults:', result.error.issues)
        this.settings = structuredClone(DEFAULT_SETTINGS)
      }
    }

    this.loaded = true
    this.dirty = false
    this.emit({ type: 'settings_loaded' })
  }

  /**
   * Save settings to storage
   */
  async save(): Promise<void> {
    await saveSettingsToFile(this.settings)
    this.dirty = false
    this.emit({ type: 'settings_saved' })
  }

  /**
   * Check if settings have unsaved changes
   */
  isDirty(): boolean {
    return this.dirty
  }

  /**
   * Check if settings have been loaded
   */
  isLoaded(): boolean {
    return this.loaded
  }

  // ==========================================================================
  // Get / Set
  // ==========================================================================

  /**
   * Get settings for a category
   */
  get<K extends SettingsCategory>(category: K): Settings[K] {
    return this.settings[category]
  }

  /**
   * Get all settings (read-only copy)
   */
  getAll(): Readonly<Settings> {
    return this.settings
  }

  /**
   * Update settings for a category
   * Performs partial update - only specified fields are changed
   */
  set<K extends SettingsCategory>(category: K, value: Partial<Settings[K]>): void {
    // Validate partial update
    const schema = PARTIAL_SCHEMAS[category]
    const result = schema.safeParse(value)

    if (!result.success) {
      throw new SettingsValidationError(category, result.error)
    }

    // Merge with existing
    const current = this.settings[category]
    const merged = { ...current, ...value }

    // Validate full category
    const fullSchema = CATEGORY_SCHEMAS[category]
    const fullResult = fullSchema.safeParse(merged)

    if (!fullResult.success) {
      throw new SettingsValidationError(category, fullResult.error)
    }

    // Apply update
    this.settings[category] = merged as Settings[K]
    this.dirty = true
    this.emit({ type: 'category_changed', category })
  }

  /**
   * Reset a category to defaults
   */
  reset(category: SettingsCategory): void {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    // biome-ignore lint/suspicious/noExplicitAny: TypeScript generic inference limitation
    ;(this.settings as any)[category] = structuredClone(DEFAULT_SETTINGS[category])
    this.dirty = true
    this.emit({ type: 'settings_reset', category })
  }

  /**
   * Reset all settings to defaults
   */
  resetAll(): void {
    this.settings = structuredClone(DEFAULT_SETTINGS)
    this.dirty = true
    this.emit({ type: 'settings_reset' })
  }

  // ==========================================================================
  // Type-Safe Getters (convenience)
  // ==========================================================================

  get provider(): ProviderSettings {
    return this.settings.provider
  }

  get agent(): AgentSettings {
    return this.settings.agent
  }

  get permissions(): PermissionSettings {
    return this.settings.permissions
  }

  get context(): ContextSettings {
    return this.settings.context
  }

  get memory(): MemorySettings {
    return this.settings.memory
  }

  get ui(): UISettings {
    return this.settings.ui
  }

  // ==========================================================================
  // Events
  // ==========================================================================

  /**
   * Subscribe to settings events
   * Returns cleanup function
   */
  on(listener: SettingsEventListener): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  /**
   * Emit event to all listeners
   */
  private emit(event: SettingsEvent): void {
    for (const listener of this.listeners) {
      try {
        listener(event)
      } catch (err) {
        console.warn('Settings event listener error:', err)
      }
    }
  }

  // ==========================================================================
  // Helpers
  // ==========================================================================

  /**
   * Deep merge settings with defaults
   */
  private mergeSettings(defaults: Settings, stored: Partial<Settings>): Settings {
    const merged: Settings = structuredClone(defaults)

    for (const key of Object.keys(stored) as SettingsCategory[]) {
      if (stored[key] !== undefined && merged[key] !== undefined) {
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        // biome-ignore lint/suspicious/noExplicitAny: TypeScript generic inference limitation
        ;(merged as any)[key] = {
          ...merged[key],
          ...stored[key],
        }
      }
    }

    return merged
  }
}

// ============================================================================
// Validation Error
// ============================================================================

/**
 * Error thrown when settings validation fails
 */
export class SettingsValidationError extends Error {
  constructor(
    public readonly category: SettingsCategory,
    public readonly zodError: ZodError
  ) {
    const issues = zodError.issues.map((i) => `${i.path.join('.')}: ${i.message}`).join(', ')
    super(`Invalid ${category} settings: ${issues}`)
    this.name = 'SettingsValidationError'
  }
}

// ============================================================================
// Singleton Instance
// ============================================================================

let _instance: SettingsManager | null = null

/**
 * Get the global settings manager instance
 */
export function getSettingsManager(): SettingsManager {
  if (!_instance) {
    _instance = new SettingsManager()
  }
  return _instance
}

/**
 * Set the global settings manager instance (for testing)
 */
export function setSettingsManager(manager: SettingsManager | null): void {
  _instance = manager
}

// ============================================================================
// Factory Function
// ============================================================================

/**
 * Create a new settings manager instance
 */
export function createSettingsManager(): SettingsManager {
  return new SettingsManager()
}
