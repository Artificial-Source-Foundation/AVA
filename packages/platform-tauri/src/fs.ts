/**
 * Tauri File System Implementation
 */

import type { DirEntry, FileStat, IFileSystem } from '@ava/core-v2'
import {
  readFile as readBinaryFile,
  readTextFile,
  exists as tauriExists,
  mkdir as tauriMkdir,
  readDir as tauriReadDir,
  remove as tauriRemove,
  stat as tauriStat,
  writeFile as writeBinaryFile,
  writeTextFile,
} from '@tauri-apps/plugin-fs'

// Directories to skip during glob traversal
const SKIP_DIRS = new Set([
  'node_modules',
  '.git',
  '.svn',
  '.hg',
  'dist',
  'build',
  '.next',
  '.nuxt',
  '.output',
  'coverage',
  '.cache',
  '__pycache__',
  '.pytest_cache',
  'target', // Rust
  'vendor', // Go/PHP
])

export class TauriFileSystem implements IFileSystem {
  async readFile(path: string): Promise<string> {
    return readTextFile(path)
  }

  async readBinary(path: string, limit?: number): Promise<Uint8Array> {
    const data = await readBinaryFile(path)
    if (limit !== undefined && limit > 0 && data.length > limit) {
      return data.slice(0, limit)
    }
    return data
  }

  async writeFile(path: string, content: string): Promise<void> {
    await writeTextFile(path, content)
  }

  async writeBinary(path: string, content: Uint8Array): Promise<void> {
    await writeBinaryFile(path, content)
  }

  async readDir(path: string): Promise<string[]> {
    const entries = await tauriReadDir(path)
    return entries.map((e) => e.name)
  }

  async readDirWithTypes(path: string): Promise<DirEntry[]> {
    const entries = await tauriReadDir(path)
    return entries.map((e) => ({
      name: e.name,
      isFile: e.isFile,
      isDirectory: e.isDirectory,
    }))
  }

  async stat(path: string): Promise<FileStat> {
    const info = await tauriStat(path)
    return {
      isFile: info.isFile,
      isDirectory: info.isDirectory,
      size: info.size,
      mtime: info.mtime ? new Date(info.mtime).getTime() : Date.now(),
    }
  }

  async exists(path: string): Promise<boolean> {
    return tauriExists(path)
  }

  async isFile(path: string): Promise<boolean> {
    try {
      const info = await tauriStat(path)
      return info.isFile
    } catch {
      return false
    }
  }

  async isDirectory(path: string): Promise<boolean> {
    try {
      const info = await tauriStat(path)
      return info.isDirectory
    } catch {
      return false
    }
  }

  async mkdir(path: string): Promise<void> {
    await tauriMkdir(path, { recursive: true })
  }

  async remove(path: string): Promise<void> {
    await tauriRemove(path, { recursive: true })
  }

  async realpath(path: string): Promise<string> {
    // Resolve symlinks by reading the file/directory and checking for symlink
    // Tauri's stat() can identify symlinks via isSymlink property
    try {
      const info = await tauriStat(path)

      // If it's a symlink, resolve it
      if ('isSymlink' in info && info.isSymlink) {
        // In Tauri, we can't easily resolve symlinks to their targets
        // without additional APIs. For now, return the path with a warning.
        console.warn(`Symlink detected at ${path} but cannot resolve target in Tauri`)
      }

      // Normalize the path: remove redundant separators and resolve . and ..
      return this.normalizePath(path)
    } catch {
      // If stat fails, return normalized path anyway
      return this.normalizePath(path)
    }
  }

  /**
   * Normalize a path by removing redundant separators and resolving . and ..
   * This is a basic normalization - not as complete as Node's path.normalize
   */
  private normalizePath(path: string): string {
    // Split into components
    const parts = path.split('/')
    const normalized: string[] = []

    for (const part of parts) {
      if (part === '' || part === '.') {
      } else if (part === '..') {
        // Go up one directory
        if (normalized.length > 0 && normalized[normalized.length - 1] !== '..') {
          normalized.pop()
        } else if (!path.startsWith('/')) {
          // Relative path with .. at start
          normalized.push('..')
        }
      } else {
        normalized.push(part)
      }
    }

    // Preserve leading slash for absolute paths
    const prefix = path.startsWith('/') ? '/' : ''
    return prefix + normalized.join('/')
  }

  async glob(pattern: string, cwd: string): Promise<string[]> {
    const matches: string[] = []
    await this.walkDirectory(cwd, '', pattern, matches)
    return matches
  }

  // ============================================================================
  // Private Helpers
  // ============================================================================

  private async walkDirectory(
    base: string,
    relativePath: string,
    pattern: string,
    matches: string[],
    maxResults = 1000
  ): Promise<void> {
    if (matches.length >= maxResults) return

    const fullPath = relativePath ? `${base}/${relativePath}` : base

    try {
      const entries = await tauriReadDir(fullPath)

      for (const entry of entries) {
        if (matches.length >= maxResults) break

        const relPath = relativePath ? `${relativePath}/${entry.name}` : entry.name

        if (entry.isFile) {
          if (this.matchesGlob(relPath, pattern)) {
            matches.push(relPath)
          }
        } else if (entry.isDirectory && !SKIP_DIRS.has(entry.name)) {
          // Check if pattern could match files in this directory
          if (this.couldMatchInDir(relPath, pattern)) {
            await this.walkDirectory(base, relPath, pattern, matches, maxResults)
          }
        }
      }
    } catch {
      // Directory might not exist or be accessible
    }
  }

  /**
   * Simple glob pattern matching
   * Supports: *, **, ?, {a,b}, [abc]
   */
  private matchesGlob(path: string, pattern: string): boolean {
    // Convert glob to regex
    let regex = '^'
    let i = 0

    while (i < pattern.length) {
      const char = pattern[i]

      if (char === '*') {
        if (pattern[i + 1] === '*') {
          // ** matches any path segment
          if (pattern[i + 2] === '/') {
            regex += '(?:.*/)?'
            i += 3
          } else {
            regex += '.*'
            i += 2
          }
        } else {
          // * matches anything except /
          regex += '[^/]*'
          i++
        }
      } else if (char === '?') {
        regex += '[^/]'
        i++
      } else if (char === '{') {
        // {a,b,c} alternation
        const end = pattern.indexOf('}', i)
        if (end !== -1) {
          const options = pattern.slice(i + 1, end).split(',')
          regex += `(?:${options.map(this.escapeRegex).join('|')})`
          i = end + 1
        } else {
          regex += '\\{'
          i++
        }
      } else if (char === '[') {
        // [abc] character class
        const end = pattern.indexOf(']', i)
        if (end !== -1) {
          regex += pattern.slice(i, end + 1)
          i = end + 1
        } else {
          regex += '\\['
          i++
        }
      } else {
        // Escape special regex chars
        regex += this.escapeRegex(char)
        i++
      }
    }

    regex += '$'

    try {
      return new RegExp(regex).test(path)
    } catch {
      return false
    }
  }

  private escapeRegex(str: string): string {
    return str.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
  }

  /**
   * Check if pattern could potentially match files in a directory
   */
  private couldMatchInDir(dirPath: string, pattern: string): boolean {
    // ** can match any depth
    if (pattern.includes('**')) return true

    // Check if dir path is a prefix of the pattern's directory part
    const patternDir = pattern.split('/').slice(0, -1).join('/')
    if (!patternDir) return true // Pattern has no dir part

    // Simple prefix check
    if (patternDir.startsWith(dirPath)) return true
    if (dirPath.startsWith(patternDir.split('*')[0])) return true

    return false
  }
}
