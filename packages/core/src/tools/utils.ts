/**
 * Tool Utilities
 * Shared helper functions for tool implementations
 */

import { getPlatform } from '../platform.js'

// ============================================================================
// Binary Detection
// ============================================================================

// ============================================================================
// Binary Check Types
// ============================================================================

/**
 * Result of binary file detection
 */
export interface BinaryCheckResult {
  /** Whether the file is binary */
  isBinary: boolean
  /** Reason for the determination */
  reason: 'extension' | 'null_bytes' | 'non_printable_ratio' | 'none'
  /** Confidence level */
  confidence: 'high' | 'medium' | 'low'
}

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
 * Uses platform abstraction for file reading
 */
export async function isBinaryFile(path: string): Promise<boolean> {
  // Check extension first (fast path)
  if (isBinaryExtension(path)) {
    return true
  }

  // Check content for null bytes (first 4KB)
  try {
    const bytes = await getPlatform().fs.readBinary(path, 4096)

    for (let i = 0; i < bytes.length; i++) {
      if (bytes[i] === 0) {
        return true
      }
    }

    // Also check for high ratio of non-printable characters
    let nonPrintable = 0
    for (let i = 0; i < bytes.length; i++) {
      const byte = bytes[i]
      // Non-printable: not tab (9), newline (10), carriage return (13), or printable ASCII (32-126)
      if (byte !== 9 && byte !== 10 && byte !== 13 && (byte < 32 || byte > 126)) {
        nonPrintable++
      }
    }

    // If more than 30% non-printable, consider binary
    return bytes.length > 0 && nonPrintable / bytes.length > 0.3
  } catch {
    // If we can't read the file, assume text
    return false
  }
}

/**
 * Check if file is binary and return detailed result
 * Enhanced version with structured output
 */
export async function checkBinaryFile(path: string): Promise<BinaryCheckResult> {
  // Check extension first (fast path, high confidence)
  if (isBinaryExtension(path)) {
    return {
      isBinary: true,
      reason: 'extension',
      confidence: 'high',
    }
  }

  // Check content for null bytes and non-printable ratio
  try {
    const bytes = await getPlatform().fs.readBinary(path, 4096)

    // Empty file is not binary
    if (bytes.length === 0) {
      return {
        isBinary: false,
        reason: 'none',
        confidence: 'high',
      }
    }

    // Check for null bytes (definitive binary indicator)
    for (let i = 0; i < bytes.length; i++) {
      if (bytes[i] === 0) {
        return {
          isBinary: true,
          reason: 'null_bytes',
          confidence: 'high',
        }
      }
    }

    // Check for high ratio of non-printable characters
    let nonPrintable = 0
    for (let i = 0; i < bytes.length; i++) {
      const byte = bytes[i]
      // Non-printable: not tab (9), newline (10), carriage return (13), or printable ASCII (32-126)
      if (byte !== 9 && byte !== 10 && byte !== 13 && (byte < 32 || byte > 126)) {
        nonPrintable++
      }
    }

    const ratio = nonPrintable / bytes.length

    // If more than 30% non-printable, consider binary
    if (ratio > 0.3) {
      return {
        isBinary: true,
        reason: 'non_printable_ratio',
        confidence: ratio > 0.5 ? 'high' : 'medium',
      }
    }

    return {
      isBinary: false,
      reason: 'none',
      confidence: 'high',
    }
  } catch {
    // If we can't read the file, assume text with low confidence
    return {
      isBinary: false,
      reason: 'none',
      confidence: 'low',
    }
  }
}

/**
 * Check if byte array contains binary content (null bytes)
 * Used for detecting binary output from shell commands
 */
export function isBinaryOutput(chunk: Uint8Array): boolean {
  for (let i = 0; i < chunk.length; i++) {
    if (chunk[i] === 0) {
      return true
    }
  }
  return false
}

// ============================================================================
// Path Utilities
// ============================================================================

/**
 * Resolve path relative to working directory
 */
export function resolvePath(path: string, workingDirectory: string): string {
  // Already absolute (Unix)
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
  return `${str.slice(0, maxLength - 3)}...`
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

// ============================================================================
// Interactive Command Detection
// ============================================================================

/**
 * Commands that typically require a PTY for proper operation.
 * These commands expect terminal capabilities like:
 * - Cursor movement (vim, nano, less)
 * - Raw input mode (ssh, python REPL)
 * - Special key handling (top, htop)
 *
 * Based on patterns from Gemini CLI and OpenCode.
 */
const INTERACTIVE_COMMANDS = new Set([
  // Editors
  'vim',
  'nvim',
  'vi',
  'nano',
  'pico',
  'emacs',
  // Pagers
  'less',
  'more',
  'most',
  // Process monitors
  'top',
  'htop',
  'btop',
  'watch',
  // REPLs
  'python',
  'python3',
  'node',
  'irb',
  'ghci',
  'lua',
  'perl',
  'php',
  'erl',
  'iex',
  // Database CLIs
  'psql',
  'mysql',
  'sqlite3',
  'redis-cli',
  'mongosh',
  // SSH and remote shells
  'ssh',
  'telnet',
  'ftp',
  'sftp',
  // Other interactive tools
  'gdb',
  'lldb',
  'screen',
  'tmux',
  'zsh',
  'bash',
  'fish',
])

/**
 * Check if a command requires a PTY (pseudo-terminal).
 *
 * @param command - The command string to check
 * @returns true if the command is interactive and needs PTY support
 */
export function isInteractiveCommand(command: string): boolean {
  // Extract the base command (first word)
  const trimmed = command.trim()
  const firstWord = trimmed.split(/\s+/)[0]

  // Get just the command name (handle paths like /usr/bin/vim)
  const cmdName = firstWord.split('/').pop() ?? firstWord

  // Check if the base command is in our interactive list
  if (INTERACTIVE_COMMANDS.has(cmdName)) {
    return true
  }

  // Check for interactive flags that suggest PTY is needed
  if (trimmed.includes(' -i') || trimmed.includes(' --interactive')) {
    return true
  }

  // Check for specific patterns that indicate interactivity
  if (
    trimmed.startsWith('docker run -it') ||
    trimmed.startsWith('docker exec -it') ||
    trimmed.includes('docker run --interactive')
  ) {
    return true
  }

  return false
}

/**
 * Get the list of known interactive commands
 * Useful for debugging or documentation
 */
export function getInteractiveCommands(): ReadonlySet<string> {
  return INTERACTIVE_COMMANDS
}

// ============================================================================
// File Suggestions (Typo Detection)
// ============================================================================

/**
 * File suggestion with similarity score
 */
export interface FileSuggestion {
  /** Suggested file path */
  path: string
  /** Similarity score (0-1, higher is more similar) */
  similarity: number
  /** Reason for suggestion */
  reason: 'similar_name' | 'same_extension' | 'common_typo'
}

/**
 * Calculate Levenshtein distance between two strings
 */
function levenshteinDistance(a: string, b: string): number {
  if (a.length === 0) return b.length
  if (b.length === 0) return a.length

  const matrix: number[][] = []

  // Initialize matrix
  for (let i = 0; i <= b.length; i++) {
    matrix[i] = [i]
  }
  for (let j = 0; j <= a.length; j++) {
    matrix[0][j] = j
  }

  // Fill matrix
  for (let i = 1; i <= b.length; i++) {
    for (let j = 1; j <= a.length; j++) {
      const cost = a[j - 1] === b[i - 1] ? 0 : 1
      matrix[i][j] = Math.min(
        matrix[i - 1][j] + 1, // deletion
        matrix[i][j - 1] + 1, // insertion
        matrix[i - 1][j - 1] + cost // substitution
      )
    }
  }

  return matrix[b.length][a.length]
}

/**
 * Calculate string similarity (0-1)
 */
function stringSimilarity(a: string, b: string): number {
  const maxLen = Math.max(a.length, b.length)
  if (maxLen === 0) return 1
  const distance = levenshteinDistance(a.toLowerCase(), b.toLowerCase())
  return 1 - distance / maxLen
}

/**
 * Find similar files when a file is not found
 * Returns suggestions sorted by similarity
 */
export async function findSimilarFiles(
  notFoundPath: string,
  workingDirectory: string,
  maxSuggestions = 3
): Promise<FileSuggestion[]> {
  const fs = getPlatform().fs
  const suggestions: FileSuggestion[] = []

  // Extract filename and directory from the not found path
  const fullPath = resolvePath(notFoundPath, workingDirectory)
  const lastSlash = fullPath.lastIndexOf('/')
  const directory = lastSlash >= 0 ? fullPath.substring(0, lastSlash) : workingDirectory
  const filename = lastSlash >= 0 ? fullPath.substring(lastSlash + 1) : notFoundPath

  // Get extension for matching
  const dotIndex = filename.lastIndexOf('.')
  const extension = dotIndex >= 0 ? filename.substring(dotIndex) : ''
  const basename = dotIndex >= 0 ? filename.substring(0, dotIndex) : filename

  try {
    // List files in the directory
    const entries = await fs.readDirWithTypes(directory)

    for (const entry of entries) {
      // Skip directories
      if (entry.isDirectory) continue

      // Calculate similarity
      const similarity = stringSimilarity(filename, entry.name)

      // Extract entry extension
      const entryDotIndex = entry.name.lastIndexOf('.')
      const entryExtension = entryDotIndex >= 0 ? entry.name.substring(entryDotIndex) : ''
      const entryBasename = entryDotIndex >= 0 ? entry.name.substring(0, entryDotIndex) : entry.name

      // Determine reason and adjust score
      let reason: FileSuggestion['reason'] = 'similar_name'
      let adjustedSimilarity = similarity

      // Boost score if extension matches
      if (extension && extension === entryExtension) {
        adjustedSimilarity = Math.min(1, adjustedSimilarity + 0.1)
        if (similarity < 0.5) {
          reason = 'same_extension'
        }
      }

      // Check for common typos (case differences, extra/missing chars)
      if (basename.toLowerCase() === entryBasename.toLowerCase()) {
        adjustedSimilarity = Math.max(0.9, adjustedSimilarity)
        reason = 'common_typo'
      }

      // Only suggest if reasonably similar
      if (adjustedSimilarity >= 0.4) {
        suggestions.push({
          path: `${directory}/${entry.name}`,
          similarity: adjustedSimilarity,
          reason,
        })
      }
    }

    // Sort by similarity (descending) and limit
    return suggestions.sort((a, b) => b.similarity - a.similarity).slice(0, maxSuggestions)
  } catch {
    // Directory doesn't exist or can't be read
    return []
  }
}

/**
 * Format suggestions for display
 */
export function formatSuggestions(suggestions: FileSuggestion[]): string {
  if (suggestions.length === 0) {
    return ''
  }

  const lines = ['Did you mean:']
  for (const s of suggestions) {
    lines.push(`  - ${s.path}`)
  }

  return lines.join('\n')
}
