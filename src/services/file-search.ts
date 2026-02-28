/**
 * File Search Service
 *
 * Recursively walks a project directory to build a flat file list for @ mentions.
 * Caches results per directory, auto-expires after 30 seconds.
 */

import { isTauri } from '@tauri-apps/api/core'

// ============================================================================
// Types
// ============================================================================

export interface SearchableFile {
  /** File name (e.g., "App.tsx") */
  name: string
  /** Relative path from project root (e.g., "src/App.tsx") */
  relative: string
  /** Absolute path */
  absolute: string
  /** Is directory */
  isDir: boolean
}

// ============================================================================
// Cache
// ============================================================================

interface CacheEntry {
  files: SearchableFile[]
  timestamp: number
}

const CACHE_TTL = 30_000 // 30 seconds
const cache = new Map<string, CacheEntry>()

// ============================================================================
// Ignored patterns
// ============================================================================

const IGNORED_DIRS = new Set([
  'node_modules',
  '.git',
  'dist',
  'build',
  'target',
  '__pycache__',
  '.next',
  '.nuxt',
  '.svelte-kit',
  '.turbo',
  '.cache',
  'coverage',
  '.DS_Store',
])

function shouldSkip(name: string): boolean {
  if (name.startsWith('.')) return true
  return IGNORED_DIRS.has(name)
}

// ============================================================================
// Recursive walker
// ============================================================================

async function walkDir(
  fsModule: { readDir: (path: string) => Promise<Array<{ name: string; isDirectory: boolean }>> },
  dirPath: string,
  rootPath: string,
  depth: number,
  maxDepth: number,
  results: SearchableFile[]
): Promise<void> {
  if (depth > maxDepth) return

  try {
    const entries = await fsModule.readDir(dirPath)

    for (const entry of entries) {
      if (shouldSkip(entry.name)) continue

      const absolute = `${dirPath}/${entry.name}`
      const relative = absolute.slice(rootPath.length + 1)

      results.push({
        name: entry.name,
        relative,
        absolute,
        isDir: entry.isDirectory,
      })

      if (entry.isDirectory && depth < maxDepth) {
        await walkDir(fsModule, absolute, rootPath, depth + 1, maxDepth, results)
      }
    }
  } catch {
    // Permission denied or other FS error — skip silently
  }
}

// ============================================================================
// Public API
// ============================================================================

/**
 * Get all files in a project directory (cached, max 4 levels deep).
 * Returns empty array in non-Tauri environments.
 */
export async function getProjectFiles(projectDir: string): Promise<SearchableFile[]> {
  // Check cache
  const cached = cache.get(projectDir)
  if (cached && Date.now() - cached.timestamp < CACHE_TTL) {
    return cached.files
  }

  if (!isTauri()) return []

  try {
    const fs = await import('@tauri-apps/plugin-fs')
    const files: SearchableFile[] = []
    await walkDir(fs, projectDir, projectDir, 0, 4, files)

    // Sort: directories first, then alphabetical
    files.sort((a, b) => {
      if (a.isDir !== b.isDir) return a.isDir ? -1 : 1
      return a.relative.localeCompare(b.relative, undefined, { sensitivity: 'base' })
    })

    cache.set(projectDir, { files, timestamp: Date.now() })
    return files
  } catch {
    return []
  }
}

/**
 * Fuzzy-filter files by query. Matches against relative path.
 * Returns top N results.
 */
export function filterFiles(files: SearchableFile[], query: string, limit = 12): SearchableFile[] {
  if (!query) return files.slice(0, limit)

  const lower = query.toLowerCase()
  const scored: Array<{ file: SearchableFile; score: number }> = []

  for (const file of files) {
    const path = file.relative.toLowerCase()
    const name = file.name.toLowerCase()

    // Exact name match — highest score
    if (name === lower) {
      scored.push({ file, score: 100 })
      continue
    }

    // Name starts with query
    if (name.startsWith(lower)) {
      scored.push({ file, score: 80 })
      continue
    }

    // Name contains query
    if (name.includes(lower)) {
      scored.push({ file, score: 60 })
      continue
    }

    // Path contains query
    if (path.includes(lower)) {
      scored.push({ file, score: 40 })
      continue
    }

    // Fuzzy: all query chars appear in order in the path
    let qi = 0
    for (let pi = 0; pi < path.length && qi < lower.length; pi++) {
      if (path[pi] === lower[qi]) qi++
    }
    if (qi === lower.length) {
      scored.push({ file, score: 20 })
    }
  }

  scored.sort((a, b) => b.score - a.score || a.file.relative.localeCompare(b.file.relative))
  return scored.slice(0, limit).map((s) => s.file)
}

/** Invalidate cache for a project directory */
export function invalidateFileCache(projectDir: string): void {
  cache.delete(projectDir)
}
