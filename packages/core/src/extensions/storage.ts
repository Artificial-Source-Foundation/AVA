/**
 * Extension Storage
 * File system operations for extension installation and persistence.
 *
 * Storage layout:
 * ~/.ava/extensions/
 *   {name}/
 *     ava-extension.json          - Extension config
 *     .ava-extension-install.json - Install metadata
 *     AVA.md                       - Context file (optional)
 *     ...                             - Extension assets
 *   extension-enablement.json         - Global enablement state
 */

import { existsSync } from 'node:fs'
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { join } from 'node:path'
import type { EnablementData, InstallMetadata } from './types.js'

// ============================================================================
// Constants
// ============================================================================

const CONFIG_DIR = '.ava'
const EXTENSIONS_DIR = 'extensions'
const INSTALL_METADATA_FILENAME = '.ava-extension-install.json'
const ENABLEMENT_FILENAME = 'extension-enablement.json'

export { INSTALL_METADATA_FILENAME }

// ============================================================================
// Extension Storage
// ============================================================================

/**
 * Manages file system operations for a single extension.
 */
export class ExtensionStorage {
  private readonly extensionDir: string

  constructor(name: string, baseDir?: string) {
    const base = baseDir ?? ExtensionStorage.getUserExtensionsDir()
    this.extensionDir = join(base, name)
  }

  /**
   * Get the extensions base directory for the current user
   */
  static getUserExtensionsDir(): string {
    const home = process.env.HOME ?? process.env.USERPROFILE ?? '.'
    return join(home, CONFIG_DIR, EXTENSIONS_DIR)
  }

  /**
   * Get the absolute path to this extension's directory
   */
  getExtensionDir(): string {
    return this.extensionDir
  }

  /**
   * Check if the extension directory exists
   */
  exists(): boolean {
    return existsSync(this.extensionDir)
  }

  /**
   * Ensure the extension directory exists
   */
  async ensureDir(): Promise<void> {
    await mkdir(this.extensionDir, { recursive: true })
  }

  /**
   * Write install metadata to disk
   */
  async writeMetadata(metadata: InstallMetadata): Promise<void> {
    await this.ensureDir()
    const metadataPath = join(this.extensionDir, INSTALL_METADATA_FILENAME)
    await writeFile(metadataPath, JSON.stringify(metadata, null, 2), 'utf-8')
  }

  /**
   * Read install metadata from disk
   */
  readMetadata(): InstallMetadata | undefined {
    const metadataPath = join(this.extensionDir, INSTALL_METADATA_FILENAME)
    try {
      if (!existsSync(metadataPath)) return undefined
      // Sync read for simplicity in loading path
      const { readFileSync } = require('node:fs') as typeof import('node:fs')
      const content = readFileSync(metadataPath, 'utf-8')
      return JSON.parse(content) as InstallMetadata
    } catch {
      return undefined
    }
  }

  /**
   * Remove the extension directory entirely
   */
  async remove(): Promise<void> {
    if (this.exists()) {
      await rm(this.extensionDir, { recursive: true, force: true })
    }
  }
}

// ============================================================================
// Enablement Persistence
// ============================================================================

/**
 * Load extension enablement state from disk.
 */
export async function loadEnablement(enablementPath?: string): Promise<EnablementData> {
  const filePath = enablementPath ?? getDefaultEnablementPath()
  try {
    const content = await readFile(filePath, 'utf-8')
    const data = JSON.parse(content) as EnablementData
    if (data.version === 1 && typeof data.extensions === 'object') {
      return data
    }
  } catch {
    // File doesn't exist or is corrupted - return empty state
  }
  return { version: 1, extensions: {} }
}

/**
 * Save extension enablement state to disk.
 */
export async function saveEnablement(data: EnablementData, enablementPath?: string): Promise<void> {
  const filePath = enablementPath ?? getDefaultEnablementPath()
  await mkdir(join(filePath, '..'), { recursive: true })
  await writeFile(filePath, JSON.stringify(data, null, 2), 'utf-8')
}

/**
 * Get the default enablement file path
 */
function getDefaultEnablementPath(): string {
  return join(ExtensionStorage.getUserExtensionsDir(), ENABLEMENT_FILENAME)
}
