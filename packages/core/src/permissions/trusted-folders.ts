/**
 * Trusted Folders
 * Auto-approve file operations within user-designated trusted directories.
 *
 * Users can mark directories as trusted, which causes file operations
 * within those directories to be auto-approved without confirmation.
 *
 * Storage: ~/.ava/trusted-folders.json
 *
 * Usage:
 * ```ts
 * const manager = getTrustedFolderManager()
 * manager.addFolder('/home/user/my-project')
 *
 * // Check if a path is within a trusted folder
 * manager.isTrusted('/home/user/my-project/src/app.ts') // true
 * manager.isTrusted('/etc/passwd') // false
 * ```
 */

import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { dirname, join, normalize, resolve } from 'node:path'

// ============================================================================
// Types
// ============================================================================

/**
 * A trusted folder entry
 */
export interface TrustedFolder {
  /** Absolute path to the trusted directory */
  path: string
  /** When the folder was added */
  addedAt: number
  /** Optional description of why it's trusted */
  reason?: string
}

/**
 * Trust check result
 */
export interface TrustCheckResult {
  /** Whether the path is within a trusted folder */
  trusted: boolean
  /** The matching trusted folder (if trusted) */
  folder?: TrustedFolder
}

/**
 * Serialized storage format
 */
interface TrustedFoldersData {
  version: 1
  folders: TrustedFolder[]
}

// ============================================================================
// Manager
// ============================================================================

/**
 * Manages trusted folder designations.
 * Trusted folders auto-approve file operations within their boundaries.
 */
export class TrustedFolderManager {
  private folders: Map<string, TrustedFolder> = new Map()
  private storagePath: string
  private loaded = false

  constructor(storagePath?: string) {
    this.storagePath = storagePath ?? getDefaultStoragePath()
  }

  // ==========================================================================
  // Initialization
  // ==========================================================================

  /**
   * Load trusted folders from disk
   */
  async load(): Promise<void> {
    try {
      const data = await readFile(this.storagePath, 'utf-8')
      const parsed = JSON.parse(data) as TrustedFoldersData

      if (parsed.version === 1 && Array.isArray(parsed.folders)) {
        this.folders.clear()
        for (const folder of parsed.folders) {
          const normalized = normalize(folder.path)
          this.folders.set(normalized, { ...folder, path: normalized })
        }
      }
    } catch (err) {
      if (!isNotFoundError(err)) {
        console.warn('Failed to load trusted folders:', err)
      }
    }

    this.loaded = true
  }

  /**
   * Save trusted folders to disk
   */
  async save(): Promise<void> {
    const data: TrustedFoldersData = {
      version: 1,
      folders: Array.from(this.folders.values()),
    }

    await mkdir(dirname(this.storagePath), { recursive: true })
    await writeFile(this.storagePath, JSON.stringify(data, null, 2), 'utf-8')
  }

  /**
   * Ensure folders are loaded (lazy initialization)
   */
  private async ensureLoaded(): Promise<void> {
    if (!this.loaded) {
      await this.load()
    }
  }

  // ==========================================================================
  // Folder Management
  // ==========================================================================

  /**
   * Add a directory as trusted.
   *
   * @param folderPath - Absolute path to the directory
   * @param reason - Optional description
   */
  async addFolder(folderPath: string, reason?: string): Promise<void> {
    await this.ensureLoaded()

    const normalized = normalize(resolve(folderPath))
    this.folders.set(normalized, {
      path: normalized,
      addedAt: Date.now(),
      reason,
    })

    await this.save()
  }

  /**
   * Remove a trusted folder.
   *
   * @param folderPath - Path to remove
   * @returns Whether the folder was found and removed
   */
  async removeFolder(folderPath: string): Promise<boolean> {
    await this.ensureLoaded()

    const normalized = normalize(resolve(folderPath))
    const removed = this.folders.delete(normalized)

    if (removed) {
      await this.save()
    }

    return removed
  }

  /**
   * List all trusted folders.
   */
  async listFolders(): Promise<TrustedFolder[]> {
    await this.ensureLoaded()
    return Array.from(this.folders.values()).sort((a, b) => a.path.localeCompare(b.path))
  }

  // ==========================================================================
  // Trust Checking
  // ==========================================================================

  /**
   * Check if a file path is within a trusted folder.
   *
   * @param filePath - Absolute path to check
   * @returns Trust check result
   */
  async isTrusted(filePath: string): Promise<TrustCheckResult> {
    await this.ensureLoaded()

    const normalized = normalize(resolve(filePath))

    for (const folder of this.folders.values()) {
      if (isWithinDirectory(normalized, folder.path)) {
        return { trusted: true, folder }
      }
    }

    return { trusted: false }
  }

  /**
   * Check multiple paths at once.
   * Returns true only if ALL paths are within trusted folders.
   */
  async areAllTrusted(paths: string[]): Promise<boolean> {
    for (const path of paths) {
      const result = await this.isTrusted(path)
      if (!result.trusted) return false
    }
    return true
  }

  /**
   * Synchronous trust check (only works after load() has been called).
   * Used in hot paths where async is not desirable.
   */
  isTrustedSync(filePath: string): TrustCheckResult {
    if (!this.loaded) {
      return { trusted: false }
    }

    const normalized = normalize(resolve(filePath))

    for (const folder of this.folders.values()) {
      if (isWithinDirectory(normalized, folder.path)) {
        return { trusted: true, folder }
      }
    }

    return { trusted: false }
  }

  // ==========================================================================
  // Utility
  // ==========================================================================

  /**
   * Get the number of trusted folders
   */
  get size(): number {
    return this.folders.size
  }

  /**
   * Clear all trusted folders
   */
  async clear(): Promise<void> {
    this.folders.clear()
    await this.save()
  }

  /**
   * Reset state (for testing)
   */
  reset(): void {
    this.folders.clear()
    this.loaded = false
  }
}

// ============================================================================
// Path Helpers
// ============================================================================

/**
 * Check if a file path is within a directory.
 * Both paths must be normalized and absolute.
 */
function isWithinDirectory(filePath: string, dirPath: string): boolean {
  // Ensure directory path ends with separator for exact prefix matching
  const dirWithSep = dirPath.endsWith('/') ? dirPath : `${dirPath}/`

  // File is within directory if it starts with the directory path
  // Also handle the case where filePath === dirPath
  return filePath === dirPath || filePath.startsWith(dirWithSep)
}

// ============================================================================
// Storage Path
// ============================================================================

function getDefaultStoragePath(): string {
  const home = process.env.HOME ?? process.env.USERPROFILE ?? '.'
  return join(home, '.ava', 'trusted-folders.json')
}

function isNotFoundError(err: unknown): boolean {
  return err instanceof Error && 'code' in err && (err as NodeJS.ErrnoException).code === 'ENOENT'
}

// ============================================================================
// Singleton
// ============================================================================

let instance: TrustedFolderManager | null = null

/**
 * Get the global trusted folder manager
 */
export function getTrustedFolderManager(): TrustedFolderManager {
  if (!instance) {
    instance = new TrustedFolderManager()
  }
  return instance
}

/**
 * Set the global trusted folder manager (for testing)
 */
export function setTrustedFolderManager(manager: TrustedFolderManager): void {
  instance = manager
}

/**
 * Reset the global trusted folder manager (for testing)
 */
export function resetTrustedFolderManager(): void {
  instance = null
}
