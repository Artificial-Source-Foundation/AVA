/**
 * Delta9 Storage Adapter
 *
 * Abstraction layer for storage operations.
 * Currently uses file-based storage, but provides a clean interface
 * for future migration to SQLite or other backends.
 *
 * Pattern from: swarm-plugin libSQL + JSONL + Git architecture
 *
 * Storage Types:
 * - JSON: Single object storage (mission.json)
 * - JSONL: Append-only log storage (history.jsonl, events.jsonl)
 * - Text: Plain text storage (markdown files)
 */

import { existsSync, readFileSync, writeFileSync, appendFileSync, mkdirSync, unlinkSync, readdirSync } from 'node:fs'
import { join, dirname } from 'node:path'
import { getNamedLogger } from './logger.js'

const log = getNamedLogger('storage-adapter')

// =============================================================================
// Types
// =============================================================================

/**
 * Storage adapter interface
 *
 * All methods work with string keys that map to file paths.
 * Keys can include slashes for directory structure.
 */
export interface StorageAdapter {
  /**
   * Read a value by key
   * @returns The value or null if not found
   */
  read<T>(key: string): Promise<T | null>

  /**
   * Write a value by key (creates or overwrites)
   */
  write<T>(key: string, value: T): Promise<void>

  /**
   * Append a value to an existing key (for logs)
   * Creates the file if it doesn't exist.
   */
  append<T>(key: string, value: T): Promise<void>

  /**
   * List all keys with a given prefix
   */
  list(prefix: string): Promise<string[]>

  /**
   * Delete a key
   * @returns true if deleted, false if not found
   */
  delete(key: string): Promise<boolean>

  /**
   * Check if a key exists
   */
  exists(key: string): Promise<boolean>
}

/**
 * Storage options
 */
export interface StorageOptions {
  /** Base directory for storage */
  baseDir: string
  /** File extension for JSON storage */
  jsonExtension?: string
  /** File extension for JSONL storage */
  jsonlExtension?: string
  /** Pretty print JSON files */
  prettyPrint?: boolean
}

/**
 * Read options
 */
export interface ReadOptions {
  /** Parse as JSONL (array of lines) */
  jsonl?: boolean
  /** Return raw string instead of parsed JSON */
  raw?: boolean
}

/**
 * Write options
 */
export interface WriteOptions {
  /** Append as JSONL line instead of overwriting */
  append?: boolean
  /** Create parent directories if needed */
  mkdir?: boolean
}

// =============================================================================
// File Storage Adapter
// =============================================================================

/**
 * File-based storage adapter
 *
 * Maps keys to files:
 * - "mission" -> {baseDir}/mission.json
 * - "history" -> {baseDir}/history.jsonl (JSONL mode)
 * - "checkpoints/abc123" -> {baseDir}/checkpoints/abc123.json
 */
export class FileStorageAdapter implements StorageAdapter {
  private options: Required<StorageOptions>

  constructor(options: StorageOptions) {
    this.options = {
      baseDir: options.baseDir,
      jsonExtension: options.jsonExtension ?? '.json',
      jsonlExtension: options.jsonlExtension ?? '.jsonl',
      prettyPrint: options.prettyPrint ?? true,
    }
  }

  /**
   * Get the file path for a key
   */
  private getPath(key: string, isJsonl: boolean = false): string {
    const ext = isJsonl ? this.options.jsonlExtension : this.options.jsonExtension
    // Check if key already has extension
    if (key.endsWith(ext)) {
      return join(this.options.baseDir, key)
    }
    return join(this.options.baseDir, `${key}${ext}`)
  }

  /**
   * Ensure directory exists for a file path
   */
  private ensureDir(filePath: string): void {
    const dir = dirname(filePath)
    if (!existsSync(dir)) {
      mkdirSync(dir, { recursive: true })
    }
  }

  // ===========================================================================
  // StorageAdapter Implementation
  // ===========================================================================

  async read<T>(key: string): Promise<T | null> {
    // Try JSON first
    const jsonPath = this.getPath(key, false)
    if (existsSync(jsonPath)) {
      try {
        const content = readFileSync(jsonPath, 'utf-8')
        return JSON.parse(content) as T
      } catch (error) {
        log.error(`Failed to read ${key}: ${error instanceof Error ? error.message : String(error)}`)
        return null
      }
    }

    // Try JSONL
    const jsonlPath = this.getPath(key, true)
    if (existsSync(jsonlPath)) {
      try {
        const content = readFileSync(jsonlPath, 'utf-8')
        const lines = content.trim().split('\n').filter((l) => l.length > 0)
        return lines.map((line) => JSON.parse(line)) as T
      } catch (error) {
        log.error(`Failed to read ${key}: ${error instanceof Error ? error.message : String(error)}`)
        return null
      }
    }

    return null
  }

  async write<T>(key: string, value: T): Promise<void> {
    const path = this.getPath(key, false)
    this.ensureDir(path)

    try {
      const content = this.options.prettyPrint
        ? JSON.stringify(value, null, 2)
        : JSON.stringify(value)
      writeFileSync(path, content, 'utf-8')
      log.debug(`Wrote ${key}`)
    } catch (error) {
      log.error(`Failed to write ${key}: ${error instanceof Error ? error.message : String(error)}`)
      throw error
    }
  }

  async append<T>(key: string, value: T): Promise<void> {
    const path = this.getPath(key, true)
    this.ensureDir(path)

    try {
      const line = JSON.stringify(value) + '\n'
      appendFileSync(path, line, 'utf-8')
      log.debug(`Appended to ${key}`)
    } catch (error) {
      log.error(`Failed to append to ${key}: ${error instanceof Error ? error.message : String(error)}`)
      throw error
    }
  }

  async list(prefix: string): Promise<string[]> {
    const basePath = join(this.options.baseDir, prefix)
    const dir = existsSync(basePath) && this.isDirectory(basePath) ? basePath : dirname(basePath)

    if (!existsSync(dir)) {
      return []
    }

    try {
      const files = readdirSync(dir)
      const keys: string[] = []

      for (const file of files) {
        const fullPath = join(dir, file)
        if (this.isDirectory(fullPath)) {
          // Recursively list subdirectories
          const subKeys = await this.list(join(prefix, file))
          keys.push(...subKeys)
        } else if (file.endsWith(this.options.jsonExtension) || file.endsWith(this.options.jsonlExtension)) {
          // Remove extension to get key
          const key = join(prefix, file.replace(/\.(json|jsonl)$/, ''))
          keys.push(key)
        }
      }

      return keys
    } catch (error) {
      log.error(`Failed to list ${prefix}: ${error instanceof Error ? error.message : String(error)}`)
      return []
    }
  }

  async delete(key: string): Promise<boolean> {
    // Try JSON first
    const jsonPath = this.getPath(key, false)
    if (existsSync(jsonPath)) {
      try {
        unlinkSync(jsonPath)
        log.debug(`Deleted ${key}`)
        return true
      } catch (error) {
        log.error(`Failed to delete ${key}: ${error instanceof Error ? error.message : String(error)}`)
        return false
      }
    }

    // Try JSONL
    const jsonlPath = this.getPath(key, true)
    if (existsSync(jsonlPath)) {
      try {
        unlinkSync(jsonlPath)
        log.debug(`Deleted ${key}`)
        return true
      } catch (error) {
        log.error(`Failed to delete ${key}: ${error instanceof Error ? error.message : String(error)}`)
        return false
      }
    }

    return false
  }

  async exists(key: string): Promise<boolean> {
    const jsonPath = this.getPath(key, false)
    const jsonlPath = this.getPath(key, true)
    return existsSync(jsonPath) || existsSync(jsonlPath)
  }

  // ===========================================================================
  // Extended Methods (file-specific)
  // ===========================================================================

  /**
   * Read raw text content
   */
  async readRaw(key: string): Promise<string | null> {
    const jsonPath = this.getPath(key, false)
    if (existsSync(jsonPath)) {
      return readFileSync(jsonPath, 'utf-8')
    }

    const jsonlPath = this.getPath(key, true)
    if (existsSync(jsonlPath)) {
      return readFileSync(jsonlPath, 'utf-8')
    }

    return null
  }

  /**
   * Read JSONL as array of objects
   */
  async readJsonl<T>(key: string): Promise<T[]> {
    const path = this.getPath(key, true)
    if (!existsSync(path)) {
      return []
    }

    try {
      const content = readFileSync(path, 'utf-8')
      const lines = content.trim().split('\n').filter((l) => l.length > 0)
      return lines.map((line) => JSON.parse(line) as T)
    } catch (error) {
      log.error(`Failed to read JSONL ${key}: ${error instanceof Error ? error.message : String(error)}`)
      return []
    }
  }

  /**
   * Write raw text content
   */
  async writeRaw(key: string, content: string): Promise<void> {
    const path = this.getPath(key, false)
    this.ensureDir(path)
    writeFileSync(path, content, 'utf-8')
  }

  /**
   * Get the base directory
   */
  getBaseDir(): string {
    return this.options.baseDir
  }

  /**
   * Check if path is a directory
   */
  private isDirectory(path: string): boolean {
    try {
      const { statSync } = require('node:fs')
      return statSync(path).isDirectory()
    } catch {
      return false
    }
  }
}

// =============================================================================
// In-Memory Storage Adapter (for testing)
// =============================================================================

/**
 * In-memory storage adapter for testing
 */
export class MemoryStorageAdapter implements StorageAdapter {
  private data: Map<string, unknown> = new Map()

  async read<T>(key: string): Promise<T | null> {
    const value = this.data.get(key)
    return value !== undefined ? (value as T) : null
  }

  async write<T>(key: string, value: T): Promise<void> {
    this.data.set(key, value)
  }

  async append<T>(key: string, value: T): Promise<void> {
    const existing = this.data.get(key)
    if (Array.isArray(existing)) {
      existing.push(value)
    } else {
      this.data.set(key, [value])
    }
  }

  async list(prefix: string): Promise<string[]> {
    const keys: string[] = []
    for (const key of this.data.keys()) {
      if (key.startsWith(prefix)) {
        keys.push(key)
      }
    }
    return keys
  }

  async delete(key: string): Promise<boolean> {
    return this.data.delete(key)
  }

  async exists(key: string): Promise<boolean> {
    return this.data.has(key)
  }

  /**
   * Clear all data (for testing)
   */
  clear(): void {
    this.data.clear()
  }

  /**
   * Get all keys (for testing)
   */
  keys(): string[] {
    return Array.from(this.data.keys())
  }
}

// =============================================================================
// Factory & Singleton
// =============================================================================

let defaultAdapter: StorageAdapter | null = null

/**
 * Get or create the default storage adapter
 */
export function getStorageAdapter(baseDir?: string): StorageAdapter {
  if (!defaultAdapter && baseDir) {
    defaultAdapter = new FileStorageAdapter({ baseDir })
    log.info(`Storage adapter initialized at ${baseDir}`)
  }
  if (!defaultAdapter) {
    throw new Error('Storage adapter not initialized. Call with baseDir first.')
  }
  return defaultAdapter
}

/**
 * Set a custom storage adapter (for testing)
 */
export function setStorageAdapter(adapter: StorageAdapter): void {
  defaultAdapter = adapter
}

/**
 * Clear the default storage adapter (for testing)
 */
export function clearStorageAdapter(): void {
  defaultAdapter = null
}

/**
 * Create a file storage adapter
 */
export function createFileStorage(options: StorageOptions): FileStorageAdapter {
  return new FileStorageAdapter(options)
}

/**
 * Create a memory storage adapter (for testing)
 */
export function createMemoryStorage(): MemoryStorageAdapter {
  return new MemoryStorageAdapter()
}
