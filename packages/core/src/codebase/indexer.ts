/**
 * File Indexer
 * Index files in a codebase with metadata, symbols, and dependencies
 *
 * Provides fast scanning of project files with:
 * - Language detection
 * - Token estimation
 * - Incremental updates based on file hashes
 */

import { getPlatform } from '../platform.js'
import type { FileEntry, FileIndex, IndexerConfig, Language } from './types.js'
import { DEFAULT_INDEXER_CONFIG, EXTENSION_TO_LANGUAGE } from './types.js'

// ============================================================================
// File Indexer
// ============================================================================

/**
 * File Indexer
 *
 * Scans a project directory and creates a searchable index of files.
 * Supports incremental updates by tracking file hashes.
 *
 * Usage:
 * ```typescript
 * const indexer = new FileIndexer({ rootPath: '/path/to/project' })
 * const files = await indexer.scan()
 * ```
 */
export class FileIndexer {
  private config: IndexerConfig
  private cache: Map<string, FileIndex> = new Map()

  constructor(config: IndexerConfig) {
    this.config = {
      ...DEFAULT_INDEXER_CONFIG,
      ...config,
    }
  }

  /**
   * Scan the project directory and return all indexed files
   */
  async scan(): Promise<FileEntry[]> {
    const fs = getPlatform().fs
    const files: FileEntry[] = []

    // Build glob patterns
    const includePatterns = this.config.include || ['**/*']

    for (const pattern of includePatterns) {
      try {
        const matches = await fs.glob(pattern, this.config.rootPath)

        for (const filePath of matches) {
          // Check exclusions
          if (this.shouldExclude(filePath)) continue

          // Get file info
          const entry = await this.indexFile(filePath)
          if (entry) {
            files.push(entry)
          }
        }
      } catch {
        // Skip patterns that fail
      }
    }

    return files
  }

  /**
   * Index a single file
   */
  async indexFile(absolutePath: string): Promise<FileEntry | null> {
    const fs = getPlatform().fs

    try {
      // Get file stats
      const stat = await fs.stat(absolutePath)

      // Skip directories
      if (stat.isDirectory) return null

      // Skip files over size limit
      if (this.config.maxFileSize && stat.size > this.config.maxFileSize) {
        return null
      }

      // Calculate relative path
      const relativePath = this.getRelativePath(absolutePath)

      // Detect language
      const language = this.detectLanguage(absolutePath)

      // Estimate tokens (roughly chars / 4)
      const tokens = Math.ceil(stat.size / 4)

      return {
        path: absolutePath,
        relativePath,
        size: stat.size,
        mtime: stat.mtime,
        language,
        tokens,
      }
    } catch {
      return null
    }
  }

  /**
   * Create a full file index with content hash
   */
  async indexFileFull(absolutePath: string): Promise<FileIndex | null> {
    const fs = getPlatform().fs

    // Get basic entry
    const entry = await this.indexFile(absolutePath)
    if (!entry) return null

    // Check cache for incremental updates
    if (this.config.incremental) {
      const cached = this.cache.get(absolutePath)
      if (cached && cached.mtime === entry.mtime) {
        return cached
      }
    }

    try {
      // Read content for hash
      const content = await fs.readFile(absolutePath)
      const contentHash = await this.hashContent(content)

      // Create full index (symbols and imports added by other modules)
      const index: FileIndex = {
        ...entry,
        symbols: [],
        imports: [],
        exports: [],
        contentHash,
      }

      // Cache the result
      this.cache.set(absolutePath, index)

      return index
    } catch {
      return {
        ...entry,
        symbols: [],
        imports: [],
        exports: [],
      }
    }
  }

  /**
   * Scan and create full indexes for all files
   */
  async scanFull(): Promise<FileIndex[]> {
    const basicFiles = await this.scan()
    const fullIndexes: FileIndex[] = []

    for (const entry of basicFiles) {
      const fullIndex = await this.indexFileFull(entry.path)
      if (fullIndex) {
        fullIndexes.push(fullIndex)
      }
    }

    return fullIndexes
  }

  /**
   * Get files by language
   */
  async getFilesByLanguage(language: Language): Promise<FileEntry[]> {
    const files = await this.scan()
    return files.filter((f) => f.language === language)
  }

  /**
   * Get files matching a pattern
   */
  async getFilesMatching(pattern: string): Promise<FileEntry[]> {
    const fs = getPlatform().fs
    const files: FileEntry[] = []

    try {
      const matches = await fs.glob(pattern, this.config.rootPath)

      for (const filePath of matches) {
        if (this.shouldExclude(filePath)) continue

        const entry = await this.indexFile(filePath)
        if (entry) {
          files.push(entry)
        }
      }
    } catch {
      // Return empty on error
    }

    return files
  }

  /**
   * Clear the cache
   */
  clearCache(): void {
    this.cache.clear()
  }

  /**
   * Get cache statistics
   */
  getCacheStats(): { size: number; entries: number } {
    let size = 0
    for (const index of this.cache.values()) {
      size += index.size
    }
    return {
      size,
      entries: this.cache.size,
    }
  }

  // ============================================================================
  // Private Methods
  // ============================================================================

  /**
   * Check if a file should be excluded
   */
  private shouldExclude(filePath: string): boolean {
    if (!this.config.exclude) return false

    const relativePath = this.getRelativePath(filePath)

    for (const pattern of this.config.exclude) {
      if (this.matchGlob(relativePath, pattern)) {
        return true
      }
    }

    return false
  }

  /**
   * Simple glob pattern matching
   */
  private matchGlob(path: string, pattern: string): boolean {
    // Convert glob to regex
    const regexPattern = pattern
      .replace(/\./g, '\\.')
      .replace(/\*\*/g, '.*')
      .replace(/\*/g, '[^/]*')
      .replace(/\?/g, '.')

    const regex = new RegExp(`^${regexPattern}$`)
    return regex.test(path)
  }

  /**
   * Get path relative to root
   */
  private getRelativePath(absolutePath: string): string {
    if (absolutePath.startsWith(this.config.rootPath)) {
      const relative = absolutePath.slice(this.config.rootPath.length)
      return relative.startsWith('/') ? relative.slice(1) : relative
    }
    return absolutePath
  }

  /**
   * Detect language from file extension
   */
  private detectLanguage(filePath: string): Language {
    const ext = this.getExtension(filePath)
    return EXTENSION_TO_LANGUAGE[ext] || 'unknown'
  }

  /**
   * Get file extension (with dot)
   */
  private getExtension(filePath: string): string {
    const lastDot = filePath.lastIndexOf('.')
    if (lastDot === -1) return ''
    return filePath.slice(lastDot).toLowerCase()
  }

  /**
   * Create a simple hash of content
   */
  private async hashContent(content: string): Promise<string> {
    // Simple hash using djb2 algorithm
    let hash = 5381
    for (let i = 0; i < content.length; i++) {
      hash = (hash * 33) ^ content.charCodeAt(i)
    }
    return (hash >>> 0).toString(16)
  }
}

// ============================================================================
// Convenience Functions
// ============================================================================

/**
 * Create a file indexer for a directory
 */
export function createIndexer(rootPath: string, options?: Partial<IndexerConfig>): FileIndexer {
  return new FileIndexer({
    rootPath,
    ...options,
  })
}

/**
 * Quick scan of a directory
 */
export async function quickScan(rootPath: string): Promise<FileEntry[]> {
  const indexer = createIndexer(rootPath)
  return indexer.scan()
}

/**
 * Get language statistics for a project
 */
export async function getLanguageStats(
  rootPath: string
): Promise<Map<Language, { count: number; bytes: number; tokens: number }>> {
  const indexer = createIndexer(rootPath)
  const files = await indexer.scan()

  const stats = new Map<Language, { count: number; bytes: number; tokens: number }>()

  for (const file of files) {
    const current = stats.get(file.language) || { count: 0, bytes: 0, tokens: 0 }
    stats.set(file.language, {
      count: current.count + 1,
      bytes: current.bytes + file.size,
      tokens: current.tokens + file.tokens,
    })
  }

  return stats
}
