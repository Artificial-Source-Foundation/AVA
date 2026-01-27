/**
 * Delta9 Configuration Loader
 *
 * Loads and merges configuration from multiple sources:
 * 1. Defaults (hardcoded)
 * 2. Global: ~/.config/opencode/delta9.json
 * 3. Project: .delta9/config.json (overrides)
 */

import { readFileSync, existsSync } from 'node:fs'
import type { Delta9Config } from '../types/config.js'
import { DEFAULT_CONFIG } from '../types/config.js'
import { delta9ConfigSchema } from '../schemas/config.schema.js'
import {
  getGlobalConfigPath,
  getProjectConfigPath,
  globalConfigExists,
  projectConfigExists,
} from './paths.js'
import { getNamedLogger } from './logger.js'

const log = getNamedLogger('config')

// =============================================================================
// Configuration Cache
// =============================================================================

let cachedConfig: Delta9Config | null = null
let cachedCwd: string | null = null

// =============================================================================
// Deep Merge Utility
// =============================================================================

/**
 * Deep merge two objects. Source values override target values.
 */
function deepMerge(target: Delta9Config, source: Partial<Delta9Config>): Delta9Config {
  const result = { ...target } as Record<string, unknown>

  for (const key of Object.keys(source) as (keyof Delta9Config)[]) {
    const sourceValue = source[key]
    const targetValue = target[key]

    if (
      sourceValue !== undefined &&
      sourceValue !== null &&
      typeof sourceValue === 'object' &&
      !Array.isArray(sourceValue) &&
      targetValue !== undefined &&
      targetValue !== null &&
      typeof targetValue === 'object' &&
      !Array.isArray(targetValue)
    ) {
      // Recursively merge objects (shallow for nested)
      result[key] = { ...targetValue, ...sourceValue }
    } else if (sourceValue !== undefined) {
      // Override with source value
      result[key] = sourceValue
    }
  }

  return result as unknown as Delta9Config
}

// =============================================================================
// JSON Loader
// =============================================================================

/**
 * Safely load a JSON file
 */
function loadJsonFile<T>(path: string): T | null {
  try {
    if (!existsSync(path)) {
      return null
    }
    const content = readFileSync(path, 'utf-8')
    return JSON.parse(content) as T
  } catch (error) {
    log.error(
      `Failed to load config from ${path}: ${error instanceof Error ? error.message : String(error)}`
    )
    return null
  }
}

// =============================================================================
// Configuration Loading
// =============================================================================

/**
 * Load configuration with fallbacks and merging.
 *
 * @param cwd - Current working directory (project root)
 * @param options - Loading options
 * @returns Validated configuration
 */
export function loadConfig(
  cwd: string,
  options: { useCache?: boolean; validate?: boolean } = {}
): Delta9Config {
  const { useCache = true, validate = true } = options

  // Return cached config if available and cwd matches
  if (useCache && cachedConfig && cachedCwd === cwd) {
    return cachedConfig
  }

  // Start with defaults
  let config: Delta9Config = { ...DEFAULT_CONFIG }

  // Load and merge global config
  if (globalConfigExists()) {
    const globalConfig = loadJsonFile<Partial<Delta9Config>>(getGlobalConfigPath())
    if (globalConfig) {
      config = deepMerge(config, globalConfig)
    }
  }

  // Load and merge project config
  if (projectConfigExists(cwd)) {
    const projectConfig = loadJsonFile<Partial<Delta9Config>>(getProjectConfigPath(cwd))
    if (projectConfig) {
      config = deepMerge(config, projectConfig)
    }
  }

  // Validate with Zod
  if (validate) {
    const result = delta9ConfigSchema.safeParse(config)
    if (!result.success) {
      log.error(`Configuration validation failed: ${JSON.stringify(result.error.format())}`)
      // Return defaults on validation failure
      config = { ...DEFAULT_CONFIG }
    } else {
      config = result.data
    }
  }

  // Cache the result
  cachedConfig = config
  cachedCwd = cwd

  return config
}

/**
 * Get the current cached configuration.
 * Returns defaults if not loaded.
 */
export function getConfig(): Delta9Config {
  return cachedConfig ?? DEFAULT_CONFIG
}

/**
 * Clear the configuration cache.
 * Call this when config files are modified.
 */
export function clearConfigCache(): void {
  cachedConfig = null
  cachedCwd = null
}

/**
 * Reload configuration from disk.
 */
export function reloadConfig(cwd: string): Delta9Config {
  clearConfigCache()
  return loadConfig(cwd, { useCache: false })
}

// =============================================================================
// Configuration Getters
// =============================================================================

/**
 * Get commander configuration
 */
export function getCommanderConfig(cwd: string): Delta9Config['commander'] {
  return loadConfig(cwd).commander
}

/**
 * Get council configuration
 */
export function getCouncilConfig(cwd: string): Delta9Config['council'] {
  return loadConfig(cwd).council
}

/**
 * Get operator configuration
 */
export function getOperatorConfig(cwd: string): Delta9Config['operators'] {
  return loadConfig(cwd).operators
}

/**
 * Get validator configuration
 */
export function getValidatorConfig(cwd: string): Delta9Config['validator'] {
  return loadConfig(cwd).validator
}

/**
 * Get budget configuration
 */
export function getBudgetConfig(cwd: string): Delta9Config['budget'] {
  return loadConfig(cwd).budget
}

/**
 * Get mission settings
 */
export function getMissionSettings(cwd: string): Delta9Config['mission'] {
  return loadConfig(cwd).mission
}

/**
 * Get seamless integration settings
 */
export function getSeamlessConfig(cwd: string): Delta9Config['seamless'] {
  return loadConfig(cwd).seamless
}

// =============================================================================
// Configuration Utilities
// =============================================================================

/**
 * Check if council is enabled
 */
export function isCouncilEnabled(cwd: string): boolean {
  return loadConfig(cwd).council.enabled
}

/**
 * Get enabled oracle members
 */
export function getEnabledOracles(cwd: string): Delta9Config['council']['members'] {
  return loadConfig(cwd).council.members.filter((m) => m.enabled)
}

/**
 * Check if budget tracking is enabled
 */
export function isBudgetEnabled(cwd: string): boolean {
  return loadConfig(cwd).budget.enabled
}

/**
 * Get the budget limit
 */
export function getBudgetLimit(cwd: string): number {
  return loadConfig(cwd).budget.defaultLimit
}
