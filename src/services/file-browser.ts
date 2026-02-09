/**
 * File Browser Service
 *
 * Reads directory contents via Tauri FS plugin for the sidebar explorer.
 * Uses lazy-load pattern (same as settings-fs.ts) to avoid top-level import issues.
 */

import { isTauri } from '@tauri-apps/api/core'

// ============================================================================
// Types
// ============================================================================

export interface FileEntry {
  name: string
  path: string
  isDir: boolean
  children?: FileEntry[]
}

// ============================================================================
// FS Module (lazy-loaded)
// ============================================================================

/** Lazy-load Tauri FS to avoid top-level import issues in non-Tauri env */
async function getFsModule() {
  if (!isTauri()) return null
  try {
    return await import('@tauri-apps/plugin-fs')
  } catch {
    return null
  }
}

// ============================================================================
// Public API
// ============================================================================

/** Hidden file/dir patterns to exclude from tree */
const HIDDEN_PATTERNS = [
  /^\./, // dot-files
  /^node_modules$/,
  /^dist$/,
  /^build$/,
  /^target$/,
  /^__pycache__$/,
  /^\.git$/,
]

function isHidden(name: string): boolean {
  return HIDDEN_PATTERNS.some((p) => p.test(name))
}

/**
 * Read a directory and return sorted FileEntry list.
 * Filters hidden files/dirs by default. Dirs listed first, then alphabetical.
 */
export async function readDirectory(path: string, showHidden = false): Promise<FileEntry[]> {
  const fs = await getFsModule()
  if (!fs) return []

  try {
    const entries = await fs.readDir(path)
    const result: FileEntry[] = []

    for (const entry of entries) {
      if (!showHidden && isHidden(entry.name)) continue
      result.push({
        name: entry.name,
        path: `${path}/${entry.name}`,
        isDir: entry.isDirectory,
      })
    }

    // Sort: dirs first, then alphabetical (case-insensitive)
    result.sort((a, b) => {
      if (a.isDir !== b.isDir) return a.isDir ? -1 : 1
      return a.name.localeCompare(b.name, undefined, { sensitivity: 'base' })
    })

    return result
  } catch (err) {
    console.warn('[file-browser] Failed to read directory:', path, err)
    return []
  }
}

/**
 * Read a file's text content.
 * Returns null if read fails or not in Tauri.
 */
export async function readFileContent(path: string): Promise<string | null> {
  const fs = await getFsModule()
  if (!fs) return null

  try {
    return await fs.readTextFile(path)
  } catch (err) {
    console.warn('[file-browser] Failed to read file:', path, err)
    return null
  }
}
