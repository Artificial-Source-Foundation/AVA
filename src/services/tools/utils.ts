/**
 * Tool Utilities
 * Shared helper functions for tool implementations
 */

import { readFile } from '@tauri-apps/plugin-fs'

// ============================================================================
// Binary Detection
// ============================================================================

/** Known binary file extensions */
const BINARY_EXTENSIONS = new Set([
  // Archives
  '.zip',
  '.tar',
  '.gz',
  '.bz2',
  '.xz',
  '.7z',
  '.rar',
  // Executables
  '.exe',
  '.dll',
  '.so',
  '.dylib',
  '.bin',
  // Compiled
  '.class',
  '.jar',
  '.war',
  '.pyc',
  '.pyo',
  '.o',
  '.a',
  '.obj',
  '.lib',
  '.wasm',
  // Documents
  '.pdf',
  '.doc',
  '.docx',
  '.xls',
  '.xlsx',
  '.ppt',
  '.pptx',
  '.odt',
  '.ods',
  '.odp',
  // Images
  '.png',
  '.jpg',
  '.jpeg',
  '.gif',
  '.bmp',
  '.ico',
  '.webp',
  '.tiff',
  '.psd',
  '.raw',
  // Audio
  '.mp3',
  '.wav',
  '.ogg',
  '.flac',
  '.aac',
  '.m4a',
  // Video
  '.mp4',
  '.avi',
  '.mkv',
  '.mov',
  '.wmv',
  '.webm',
  // Fonts
  '.ttf',
  '.otf',
  '.woff',
  '.woff2',
  '.eot',
  // Data
  '.sqlite',
  '.db',
  '.dat',
])

/**
 * Check if file is binary by extension
 */
export function isBinaryExtension(path: string): boolean {
  const ext = path.substring(path.lastIndexOf('.')).toLowerCase()
  return BINARY_EXTENSIONS.has(ext)
}

/**
 * Check if file is binary by content (null byte detection)
 */
export async function isBinaryFile(path: string): Promise<boolean> {
  // Check extension first (fast path)
  if (isBinaryExtension(path)) {
    return true
  }

  // Check content for null bytes (first 4KB)
  try {
    const bytes = await readFile(path)
    const sample = bytes.slice(0, 4096)

    for (let i = 0; i < sample.length; i++) {
      if (sample[i] === 0) {
        return true
      }
    }

    // Also check for high ratio of non-printable characters
    let nonPrintable = 0
    for (let i = 0; i < sample.length; i++) {
      const byte = sample[i]
      // Non-printable: not tab (9), newline (10), carriage return (13), or printable ASCII (32-126)
      if (byte !== 9 && byte !== 10 && byte !== 13 && (byte < 32 || byte > 126)) {
        nonPrintable++
      }
    }

    // If more than 30% non-printable, consider binary
    return sample.length > 0 && nonPrintable / sample.length > 0.3
  } catch {
    // If we can't read the file, assume text
    return false
  }
}

// ============================================================================
// Path Utilities
// ============================================================================

/**
 * Resolve path relative to working directory
 */
export function resolvePath(path: string, workingDirectory: string): string {
  // Already absolute
  if (path.startsWith('/')) {
    return path
  }

  // Handle Windows-style absolute paths
  if (/^[A-Za-z]:/.test(path)) {
    return path
  }

  // Handle relative paths
  if (path.startsWith('./')) {
    path = path.slice(2)
  }

  // Handle parent directory references
  const parts = path.split('/')
  const baseParts = workingDirectory.split('/')

  for (const part of parts) {
    if (part === '..') {
      baseParts.pop()
    } else if (part !== '.') {
      baseParts.push(part)
    }
  }

  return baseParts.join('/')
}

/**
 * Get relative path from base directory
 */
export function getRelativePath(path: string, baseDir: string): string {
  if (path.startsWith(baseDir)) {
    const relative = path.slice(baseDir.length)
    return relative.startsWith('/') ? relative.slice(1) : relative
  }
  return path
}

/**
 * Check if path is inside directory
 */
export function isPathInside(path: string, dir: string): boolean {
  const normalizedPath = path.replace(/\/+$/, '')
  const normalizedDir = dir.replace(/\/+$/, '')
  return normalizedPath === normalizedDir || normalizedPath.startsWith(`${normalizedDir}/`)
}

// ============================================================================
// Pattern Matching
// ============================================================================

/**
 * Match filename against glob pattern
 * Supports: * (any chars except /), ** (any chars including /), ? (single char)
 */
export function matchesGlob(path: string, pattern: string): boolean {
  // Handle {a,b,c} alternatives
  if (pattern.includes('{')) {
    const match = pattern.match(/\{([^}]+)\}/)
    if (match) {
      const alternatives = match[1].split(',')
      return alternatives.some((alt) => matchesGlob(path, pattern.replace(match[0], alt.trim())))
    }
  }

  // Convert glob to regex
  const regex = pattern
    // Escape special regex chars (except * and ?)
    .replace(/[.+^$|()[\]\\]/g, '\\$&')
    // Handle ** (any path)
    .replace(/\*\*/g, '{{GLOBSTAR}}')
    // Handle * (any chars except /)
    .replace(/\*/g, '[^/]*')
    // Handle ? (single char)
    .replace(/\?/g, '.')
    // Restore globstar
    .replace(/\{\{GLOBSTAR\}\}/g, '.*')

  // Match full path
  return new RegExp(`^${regex}$`).test(path)
}

/**
 * Check if directory should be skipped during traversal
 */
export function shouldSkipDirectory(name: string): boolean {
  // Skip hidden directories
  if (name.startsWith('.')) {
    return true
  }

  // Skip common non-code directories
  const skipDirs = new Set([
    'node_modules',
    '__pycache__',
    'venv',
    '.venv',
    'target', // Rust
    'build',
    'dist',
    'coverage',
    '.git',
    '.svn',
    '.hg',
  ])

  return skipDirs.has(name)
}

// ============================================================================
// Output Formatting
// ============================================================================

/** Default limits */
export const LIMITS = {
  MAX_RESULTS: 100,
  MAX_LINES: 2000,
  MAX_LINE_LENGTH: 2000,
  MAX_BYTES: 50 * 1024, // 50KB
} as const

/**
 * Truncate string to max length
 */
export function truncate(str: string, maxLength: number): string {
  if (str.length <= maxLength) {
    return str
  }
  return str.slice(0, maxLength - 3) + '...'
}

/**
 * Format line number with padding
 */
export function formatLineNumber(lineNum: number, totalLines: number): string {
  const width = Math.max(5, String(totalLines).length)
  return String(lineNum).padStart(width, '0')
}

// ============================================================================
// Output Truncation
// ============================================================================

export interface TruncationResult {
  content: string
  truncated: boolean
  removedLines?: number
  removedBytes?: number
}

/**
 * Truncate output to fit within limits
 * Uses dual-threshold: whichever limit is hit first
 */
export function truncateOutput(
  output: string,
  maxLines = LIMITS.MAX_LINES,
  maxBytes = LIMITS.MAX_BYTES
): TruncationResult {
  const lines = output.split('\n')
  const totalBytes = new TextEncoder().encode(output).length

  // Check if within limits
  if (lines.length <= maxLines && totalBytes <= maxBytes) {
    return { content: output, truncated: false }
  }

  // Truncate by lines first
  const truncatedLines: string[] = []
  let bytes = 0

  for (let i = 0; i < lines.length && i < maxLines; i++) {
    const lineBytes = new TextEncoder().encode(lines[i]).length + 1 // +1 for newline
    if (bytes + lineBytes > maxBytes) {
      break
    }
    truncatedLines.push(lines[i])
    bytes += lineBytes
  }

  const removedLines = lines.length - truncatedLines.length
  const removedBytes = totalBytes - bytes

  return {
    content: truncatedLines.join('\n'),
    truncated: true,
    removedLines,
    removedBytes,
  }
}
