/**
 * Extension Manager
 * Core lifecycle management for extensions.
 *
 * Responsibilities:
 * - Discover and load installed extensions from ~/.ava/extensions/
 * - Install extensions from local paths, git repos, or symlinks
 * - Enable/disable extensions per-user
 * - Emit lifecycle events
 *
 * Usage:
 * ```ts
 * const manager = new ExtensionManager({ workspaceDir: '/my/project' })
 * await manager.loadExtensions()
 *
 * // Install a new extension
 * await manager.install('/path/to/my-extension')
 *
 * // Get active extensions for MCP server configuration
 * const active = manager.getActiveExtensions()
 * ```
 */

import { existsSync, statSync } from 'node:fs'
import { cp, mkdir, readdir, stat, symlink } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import {
  CONFIG_FILENAME,
  getContextFilePaths,
  loadExtensionConfig,
  loadInstallMetadata,
  validateExtensionName,
} from './manifest.js'
import { ExtensionStorage, loadEnablement, saveEnablement } from './storage.js'
import type {
  Extension,
  ExtensionEvent,
  ExtensionEventListener,
  ExtensionManagerOptions,
  InstallMetadata,
  InstallOptions,
} from './types.js'

// ============================================================================
// Extension Manager
// ============================================================================

export class ExtensionManager {
  private extensions: Extension[] = []
  private enablement: Record<string, boolean> = {}
  private listeners: ExtensionEventListener[] = []
  private loaded = false

  private readonly workspaceDir: string
  private readonly extensionsDir: string
  private readonly enablementPath?: string

  constructor(options: ExtensionManagerOptions = {}) {
    this.workspaceDir = options.workspaceDir ?? process.cwd()
    this.extensionsDir = options.extensionsDir ?? ExtensionStorage.getUserExtensionsDir()
    this.enablementPath = options.enablementPath
  }

  // ==========================================================================
  // Lifecycle
  // ==========================================================================

  /**
   * Discover and load all installed extensions.
   * Should be called once at startup.
   */
  async loadExtensions(): Promise<Extension[]> {
    if (this.loaded) {
      return this.extensions
    }

    // Load enablement state
    const enablementData = await loadEnablement(this.enablementPath)
    this.enablement = enablementData.extensions

    // Scan extensions directory
    this.extensions = []

    if (!existsSync(this.extensionsDir)) {
      this.loaded = true
      this.emit({ type: 'loaded', extensions: [] })
      return []
    }

    let entries: string[]
    try {
      entries = await readdir(this.extensionsDir)
    } catch {
      this.loaded = true
      return []
    }

    for (const entry of entries) {
      const extensionDir = join(this.extensionsDir, entry)

      // Skip non-directories and special files
      try {
        const stats = await stat(extensionDir)
        if (!stats.isDirectory()) continue
      } catch {
        continue
      }

      // Skip if no config file
      if (!existsSync(join(extensionDir, CONFIG_FILENAME))) {
        continue
      }

      const extension = await this.loadSingleExtension(extensionDir)
      if (extension) {
        this.extensions.push(extension)
      }
    }

    this.loaded = true
    this.emit({ type: 'loaded', extensions: [...this.extensions] })
    return [...this.extensions]
  }

  /**
   * Load a single extension from a directory.
   * Returns null if loading fails.
   */
  private async loadSingleExtension(extensionDir: string): Promise<Extension | null> {
    try {
      // Resolve symlinks for linked extensions
      let effectivePath = extensionDir
      const installMetadata = loadInstallMetadata(extensionDir)
      if (installMetadata?.type === 'link' && existsSync(installMetadata.source)) {
        effectivePath = installMetadata.source
      }

      const config = await loadExtensionConfig(effectivePath)
      const contextFiles = getContextFilePaths(config, effectivePath)
      const isActive = this.isEnabled(config.name)

      return {
        name: config.name,
        version: config.version,
        description: config.description,
        path: effectivePath,
        contextFiles,
        mcpServers: config.mcpServers,
        excludeTools: config.excludeTools,
        installMetadata,
        isActive,
      }
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      console.warn(`Skipping extension in ${extensionDir}: ${msg}`)
      return null
    }
  }

  // ==========================================================================
  // Getters
  // ==========================================================================

  /**
   * Get all loaded extensions (both active and inactive).
   */
  getExtensions(): Extension[] {
    return [...this.extensions]
  }

  /**
   * Get only active (enabled) extensions.
   */
  getActiveExtensions(): Extension[] {
    return this.extensions.filter((ext) => ext.isActive)
  }

  /**
   * Find an extension by name (case-insensitive).
   */
  findExtension(name: string): Extension | undefined {
    return this.extensions.find((ext) => ext.name.toLowerCase() === name.toLowerCase())
  }

  /**
   * Whether extensions have been loaded.
   */
  get isLoaded(): boolean {
    return this.loaded
  }

  /**
   * Number of loaded extensions.
   */
  get size(): number {
    return this.extensions.length
  }

  // ==========================================================================
  // Install / Uninstall
  // ==========================================================================

  /**
   * Install an extension from a local path.
   *
   * @param source - Absolute or relative path to extension source
   * @param options - Install options
   * @returns The installed extension
   */
  async install(source: string, options: InstallOptions = {}): Promise<Extension> {
    const resolvedSource = resolve(this.workspaceDir, source)

    // Validate source exists and has config
    if (!existsSync(resolvedSource)) {
      throw new Error(`Extension source not found: ${resolvedSource}`)
    }
    const sourceStat = statSync(resolvedSource)
    if (!sourceStat.isDirectory()) {
      throw new Error(`Extension source must be a directory: ${resolvedSource}`)
    }
    if (!existsSync(join(resolvedSource, CONFIG_FILENAME))) {
      throw new Error(`No ${CONFIG_FILENAME} found in ${resolvedSource}`)
    }

    // Load and validate config
    const config = await loadExtensionConfig(resolvedSource)
    validateExtensionName(config.name)

    // Check for duplicates
    if (this.findExtension(config.name)) {
      throw new Error(`Extension "${config.name}" is already installed. Uninstall it first.`)
    }

    // Determine install type
    const installType = options.link ? 'link' : 'local'
    const metadata: InstallMetadata = {
      type: installType,
      source: resolvedSource,
      installedAt: new Date().toISOString(),
      ref: options.ref,
    }

    const storage = new ExtensionStorage(config.name, this.extensionsDir)

    if (installType === 'link') {
      // Create symlink for development
      await mkdir(this.extensionsDir, { recursive: true })
      await symlink(resolvedSource, storage.getExtensionDir(), 'dir')
      // Write metadata in the extensions dir (not the source)
      // For links, metadata is written alongside the symlink name
      await storage.writeMetadata(metadata)
    } else {
      // Copy extension to storage
      await storage.ensureDir()
      await cp(resolvedSource, storage.getExtensionDir(), { recursive: true })
      await storage.writeMetadata(metadata)
    }

    // Enable by default
    await this.setEnablement(config.name, true)

    // Load the installed extension
    const extension = await this.loadSingleExtension(storage.getExtensionDir())
    if (!extension) {
      throw new Error(`Failed to load installed extension "${config.name}"`)
    }

    this.extensions.push(extension)
    this.emit({ type: 'installed', extension })
    return extension
  }

  /**
   * Uninstall an extension by name.
   *
   * @param name - Extension name to uninstall
   */
  async uninstall(name: string): Promise<void> {
    const extension = this.findExtension(name)
    if (!extension) {
      throw new Error(`Extension "${name}" is not installed`)
    }

    // Remove from memory
    this.extensions = this.extensions.filter((ext) => ext.name.toLowerCase() !== name.toLowerCase())

    // Remove enablement entry
    delete this.enablement[name]
    await this.persistEnablement()

    // Remove from disk
    const storage = new ExtensionStorage(name, this.extensionsDir)
    await storage.remove()

    this.emit({ type: 'uninstalled', name })
  }

  // ==========================================================================
  // Enable / Disable
  // ==========================================================================

  /**
   * Enable an extension.
   */
  async enable(name: string): Promise<void> {
    const extension = this.findExtension(name)
    if (!extension) {
      throw new Error(`Extension "${name}" is not installed`)
    }

    await this.setEnablement(name, true)
    extension.isActive = true
    this.emit({ type: 'enabled', name })
  }

  /**
   * Disable an extension.
   */
  async disable(name: string): Promise<void> {
    const extension = this.findExtension(name)
    if (!extension) {
      throw new Error(`Extension "${name}" is not installed`)
    }

    await this.setEnablement(name, false)
    extension.isActive = false
    this.emit({ type: 'disabled', name })
  }

  /**
   * Check if an extension is enabled.
   * Defaults to true for extensions without explicit enablement state.
   */
  isEnabled(name: string): boolean {
    if (name in this.enablement) {
      return this.enablement[name]!
    }
    // Extensions are enabled by default
    return true
  }

  /**
   * Update and persist enablement state.
   */
  private async setEnablement(name: string, enabled: boolean): Promise<void> {
    this.enablement[name] = enabled
    await this.persistEnablement()
  }

  /**
   * Persist current enablement state to disk.
   */
  private async persistEnablement(): Promise<void> {
    await saveEnablement({ version: 1, extensions: { ...this.enablement } }, this.enablementPath)
  }

  // ==========================================================================
  // Events
  // ==========================================================================

  /**
   * Subscribe to extension events.
   * Returns an unsubscribe function.
   */
  on(listener: ExtensionEventListener): () => void {
    this.listeners.push(listener)
    return () => {
      this.listeners = this.listeners.filter((l) => l !== listener)
    }
  }

  /**
   * Emit an event to all listeners.
   */
  private emit(event: ExtensionEvent): void {
    for (const listener of this.listeners) {
      try {
        listener(event)
      } catch {
        // Don't let listener errors break the manager
      }
    }
  }

  // ==========================================================================
  // Utility
  // ==========================================================================

  /**
   * Reload all extensions from disk.
   */
  async reload(): Promise<Extension[]> {
    this.loaded = false
    this.extensions = []
    return this.loadExtensions()
  }

  /**
   * Reset manager state (for testing).
   */
  reset(): void {
    this.extensions = []
    this.enablement = {}
    this.listeners = []
    this.loaded = false
  }
}

// ============================================================================
// Singleton
// ============================================================================

let instance: ExtensionManager | null = null

/**
 * Get the global extension manager.
 */
export function getExtensionManager(): ExtensionManager {
  if (!instance) {
    instance = new ExtensionManager()
  }
  return instance
}

/**
 * Set the global extension manager (for testing).
 */
export function setExtensionManager(manager: ExtensionManager): void {
  instance = manager
}

/**
 * Reset the global extension manager (for testing).
 */
export function resetExtensionManager(): void {
  instance = null
}
