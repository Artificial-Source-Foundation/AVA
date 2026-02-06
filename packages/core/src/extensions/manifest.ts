/**
 * Extension Manifest
 * Loading and validation of extension configuration files.
 *
 * Each extension declares its capabilities in `estela-extension.json`:
 * - name and version (required)
 * - MCP servers, context files, excluded tools (optional)
 */

import { existsSync, readFileSync } from 'node:fs'
import { readFile } from 'node:fs/promises'
import { join } from 'node:path'
import type { ExtensionConfig, InstallMetadata } from './types.js'

// ============================================================================
// Constants
// ============================================================================

export const CONFIG_FILENAME = 'estela-extension.json'
export const INSTALL_METADATA_FILENAME = '.estela-extension-install.json'
export const DEFAULT_CONTEXT_FILES = ['ESTELA.md']

/** Valid extension name pattern: alphanumeric + dashes */
const NAME_PATTERN = /^[a-zA-Z0-9][-a-zA-Z0-9]*$/

// ============================================================================
// Validation
// ============================================================================

/**
 * Validate an extension name.
 * Names must be alphanumeric with dashes, starting with a letter or number.
 *
 * @throws Error if name is invalid
 */
export function validateExtensionName(name: string): void {
  if (!name || typeof name !== 'string') {
    throw new Error('Extension name is required and must be a string')
  }
  if (name.length > 64) {
    throw new Error(`Extension name too long (max 64 characters): "${name}"`)
  }
  if (!NAME_PATTERN.test(name)) {
    throw new Error(
      `Invalid extension name: "${name}". Only letters, numbers, and dashes are allowed. Must start with a letter or number.`
    )
  }
}

/**
 * Validate a full extension config.
 *
 * @throws Error if config is invalid
 */
export function validateExtensionConfig(config: unknown): asserts config is ExtensionConfig {
  if (!config || typeof config !== 'object') {
    throw new Error('Extension config must be a non-null object')
  }

  const c = config as Record<string, unknown>

  if (!c.name || typeof c.name !== 'string') {
    throw new Error('Extension config missing required field "name"')
  }

  if (!c.version || typeof c.version !== 'string') {
    throw new Error('Extension config missing required field "version"')
  }

  validateExtensionName(c.name)
}

// ============================================================================
// Loading
// ============================================================================

/**
 * Load extension config from a directory.
 *
 * @param extensionDir - Absolute path to the extension directory
 * @returns Parsed and validated config
 * @throws Error if config file is missing or invalid
 */
export async function loadExtensionConfig(extensionDir: string): Promise<ExtensionConfig> {
  const configPath = join(extensionDir, CONFIG_FILENAME)

  if (!existsSync(configPath)) {
    throw new Error(`Extension config not found: ${configPath}`)
  }

  const content = await readFile(configPath, 'utf-8')
  let config: unknown

  try {
    config = JSON.parse(content)
  } catch (err) {
    const msg = err instanceof Error ? err.message : 'Unknown parse error'
    throw new Error(`Invalid JSON in ${configPath}: ${msg}`)
  }

  validateExtensionConfig(config)
  return config
}

/**
 * Load extension config synchronously.
 */
export function loadExtensionConfigSync(extensionDir: string): ExtensionConfig {
  const configPath = join(extensionDir, CONFIG_FILENAME)

  if (!existsSync(configPath)) {
    throw new Error(`Extension config not found: ${configPath}`)
  }

  const content = readFileSync(configPath, 'utf-8')
  let config: unknown

  try {
    config = JSON.parse(content)
  } catch (err) {
    const msg = err instanceof Error ? err.message : 'Unknown parse error'
    throw new Error(`Invalid JSON in ${configPath}: ${msg}`)
  }

  validateExtensionConfig(config)
  return config
}

/**
 * Load install metadata from an extension directory.
 * Returns undefined if metadata file doesn't exist or is invalid.
 */
export function loadInstallMetadata(extensionDir: string): InstallMetadata | undefined {
  const metadataPath = join(extensionDir, INSTALL_METADATA_FILENAME)
  try {
    if (!existsSync(metadataPath)) return undefined
    const content = readFileSync(metadataPath, 'utf-8')
    return JSON.parse(content) as InstallMetadata
  } catch {
    return undefined
  }
}

// ============================================================================
// Context Files
// ============================================================================

/**
 * Resolve context file paths for an extension.
 * Returns absolute paths to context files that actually exist on disk.
 *
 * @param config - Extension config
 * @param extensionDir - Absolute path to extension directory
 * @returns Array of absolute paths to existing context files
 */
export function getContextFilePaths(config: ExtensionConfig, extensionDir: string): string[] {
  const names = getContextFileNames(config)
  return names.map((name) => join(extensionDir, name)).filter((fullPath) => existsSync(fullPath))
}

/**
 * Get context file names from config, falling back to defaults.
 */
function getContextFileNames(config: ExtensionConfig): string[] {
  if (!config.contextFiles) {
    return [...DEFAULT_CONTEXT_FILES]
  }
  if (typeof config.contextFiles === 'string') {
    return [config.contextFiles]
  }
  return config.contextFiles
}
