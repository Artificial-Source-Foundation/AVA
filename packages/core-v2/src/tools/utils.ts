/**
 * Shared tool utilities — path resolution, binary detection, output formatting.
 */

import { getPlatform } from '../platform.js'

// ─── Limits ──────────────────────────────────────────────────────────────────

export const LIMITS = {
  MAX_RESULTS: 100,
  MAX_LINES: 2000,
  MAX_LINE_LENGTH: 2000,
  MAX_BYTES: 50 * 1024, // 50KB
} as const

// ─── Path Resolution ─────────────────────────────────────────────────────────

import * as nodePath from 'node:path'

/** Resolve a path against a working directory. Pure string manipulation. */
export function resolvePath(filePath: string, cwd: string): string {
  if (nodePath.isAbsolute(filePath)) return nodePath.normalize(filePath)
  return nodePath.resolve(cwd, filePath)
}

/**
 * Resolve a path safely — guards against symlink escape.
 *
 * Absolute paths are trusted as-is (access control is handled by
 * the permissions middleware). Only relative paths are checked to
 * ensure symlinks don't escape the working directory.
 */
export async function resolvePathSafe(filePath: string, cwd: string): Promise<string> {
  const resolved = resolvePath(filePath, cwd)
  const fs = getPlatform().fs

  // Absolute paths are explicit — trust them (permissions middleware handles access)
  if (nodePath.isAbsolute(filePath)) {
    try {
      return await fs.realpath(resolved)
    } catch {
      return resolved // ENOENT for new files
    }
  }

  // Relative paths — check that symlinks don't escape the working directory
  try {
    const realResolved = await fs.realpath(resolved)
    const realCwd = await fs.realpath(cwd)
    if (!realResolved.startsWith(`${realCwd}/`) && realResolved !== realCwd) {
      throw new Error(`Path escapes working directory via symlink: ${filePath}`)
    }
    return realResolved
  } catch (err) {
    // ENOENT is OK for file creation — fall back to non-realpath result
    if (err instanceof Error && err.message.includes('ENOENT')) {
      return resolved
    }
    throw err
  }
}

// ─── Binary Detection ────────────────────────────────────────────────────────

const BINARY_EXTENSIONS = new Set([
  '.png',
  '.jpg',
  '.jpeg',
  '.gif',
  '.bmp',
  '.ico',
  '.webp',
  '.svg',
  '.mp3',
  '.mp4',
  '.avi',
  '.mov',
  '.mkv',
  '.flac',
  '.wav',
  '.ogg',
  '.pdf',
  '.doc',
  '.docx',
  '.xls',
  '.xlsx',
  '.ppt',
  '.pptx',
  '.zip',
  '.tar',
  '.gz',
  '.bz2',
  '.7z',
  '.rar',
  '.xz',
  '.exe',
  '.dll',
  '.so',
  '.dylib',
  '.bin',
  '.o',
  '.a',
  '.wasm',
  '.pyc',
  '.class',
  '.jar',
  '.ttf',
  '.otf',
  '.woff',
  '.woff2',
  '.eot',
  '.sqlite',
  '.db',
  '.sqlite3',
])

export function isBinaryExtension(filePath: string): boolean {
  const ext = nodePath.extname(filePath).toLowerCase()
  return BINARY_EXTENSIONS.has(ext)
}

export async function isBinaryFile(filePath: string): Promise<boolean> {
  if (isBinaryExtension(filePath)) return true

  const fs = getPlatform().fs
  try {
    const chunk = await fs.readBinary(filePath, 4096)
    if (chunk.length === 0) return false

    let nonPrintable = 0
    for (const byte of chunk) {
      if (byte === 0) return true // null byte = definitely binary
      if (byte !== 9 && byte !== 10 && byte !== 13 && (byte < 32 || byte > 126)) {
        nonPrintable++
      }
    }
    return nonPrintable / chunk.length > 0.3
  } catch {
    return false
  }
}

export function isBinaryOutput(chunk: Uint8Array): boolean {
  for (const byte of chunk) {
    if (byte === 0) return true
  }
  return false
}

// ─── Output Formatting ──────────────────────────────────────────────────────

export function truncate(str: string, maxLength: number): string {
  if (str.length <= maxLength) return str
  return `${str.slice(0, maxLength - 3)}...`
}

export function formatLineNumber(lineNum: number, totalLines: number): string {
  const width = Math.max(5, String(totalLines).length)
  return String(lineNum).padStart(width, '0')
}

export interface TruncationResult {
  content: string
  truncated: boolean
  removedLines: number
  removedBytes: number
}

export function truncateOutput(
  output: string,
  maxLines = LIMITS.MAX_LINES,
  maxBytes = LIMITS.MAX_BYTES
): TruncationResult {
  const lines = output.split('\n')
  let byteCount = 0
  let lineCount = 0

  for (const line of lines) {
    const lineBytes = Buffer.byteLength(line, 'utf8') + 1
    if (lineCount >= maxLines || byteCount + lineBytes > maxBytes) {
      const kept = lines.slice(0, lineCount).join('\n')
      return {
        content: kept,
        truncated: true,
        removedLines: lines.length - lineCount,
        removedBytes: Buffer.byteLength(output, 'utf8') - Buffer.byteLength(kept, 'utf8'),
      }
    }
    byteCount += lineBytes
    lineCount++
  }

  return { content: output, truncated: false, removedLines: 0, removedBytes: 0 }
}

// ─── Pattern Matching ────────────────────────────────────────────────────────

const SKIP_DIRS = new Set([
  'node_modules',
  '__pycache__',
  'venv',
  '.venv',
  'target',
  'build',
  'dist',
  'coverage',
  '.git',
  '.svn',
  '.hg',
])

export function shouldSkipDirectory(name: string): boolean {
  return name.startsWith('.') || SKIP_DIRS.has(name)
}

export function matchesGlob(filePath: string, pattern: string): boolean {
  // Handle {a,b} alternatives
  const altMatch = pattern.match(/\{([^}]+)\}/)
  if (altMatch) {
    const alternatives = altMatch[1].split(',')
    return alternatives.some((alt) =>
      matchesGlob(filePath, pattern.replace(altMatch[0], alt.trim()))
    )
  }

  // Convert glob to regex
  const regex = pattern
    .replace(/\./g, '\\.')
    .replace(/\*\*/g, '{{GLOBSTAR}}')
    .replace(/\*/g, '[^/]*')
    .replace(/\?/g, '.')
    .replace(/\{\{GLOBSTAR\}\}/g, '.*')

  return new RegExp(`^${regex}$`).test(filePath)
}
