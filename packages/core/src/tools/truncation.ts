/**
 * Output Truncation Module
 * Enhanced truncation with metrics, persistence, and configurable limits
 *
 * Based on OpenCode's output management patterns
 */

import { getPlatform } from '../platform.js'

// ============================================================================
// Constants
// ============================================================================

/** Default truncation limits */
export const TRUNCATION_LIMITS = {
  /** Maximum lines to include in output */
  MAX_LINES: 2000,
  /** Maximum bytes for output (50KB) */
  MAX_BYTES: 50 * 1024,
  /** Maximum bytes for metadata streaming (30KB) */
  MAX_METADATA_BYTES: 30 * 1024,
  /** Maximum line length before truncation */
  MAX_LINE_LENGTH: 2000,
} as const

/** Directory for storing full output files */
const OUTPUT_DIR = '.estela/tool-output'

// ============================================================================
// Types
// ============================================================================

/**
 * Result of truncation operation with full metrics
 */
export interface TruncationResult {
  /** Truncated content */
  content: string
  /** Whether content was truncated */
  truncated: boolean
  /** Number of lines removed */
  removedLines?: number
  /** Number of bytes removed */
  removedBytes?: number
  /** Original total lines */
  totalLines?: number
  /** Original total bytes */
  totalBytes?: number
  /** Path where full output was saved (if persistFull enabled) */
  outputPath?: string
}

/**
 * Options for truncation behavior
 */
export interface TruncationOptions {
  /** Maximum lines (default: 2000) */
  maxLines?: number
  /** Maximum bytes (default: 50KB) */
  maxBytes?: number
  /** Truncation direction: 'head' keeps start, 'tail' keeps end */
  direction?: 'head' | 'tail'
  /** Save full output to file when truncated */
  persistFull?: boolean
  /** Working directory for output persistence */
  workingDirectory?: string
  /** Custom output ID for persistence (default: timestamp) */
  outputId?: string
}

// ============================================================================
// Core Functions
// ============================================================================

/**
 * Truncate output with full metrics and optional persistence
 *
 * @param output - The output string to truncate
 * @param options - Truncation options
 * @returns TruncationResult with content and metrics
 *
 * @example
 * ```typescript
 * const result = await truncateOutput(largeOutput, {
 *   maxLines: 1000,
 *   maxBytes: 30 * 1024,
 *   persistFull: true,
 *   workingDirectory: '/project'
 * })
 *
 * if (result.truncated) {
 *   console.log(`Removed ${result.removedLines} lines`)
 *   console.log(`Full output at: ${result.outputPath}`)
 * }
 * ```
 */
export async function truncateOutput(
  output: string,
  options: TruncationOptions = {}
): Promise<TruncationResult> {
  const {
    maxLines = TRUNCATION_LIMITS.MAX_LINES,
    maxBytes = TRUNCATION_LIMITS.MAX_BYTES,
    direction = 'head',
    persistFull = false,
    workingDirectory,
    outputId,
  } = options

  const lines = output.split('\n')
  const totalBytes = new TextEncoder().encode(output).length
  const totalLines = lines.length

  // Check if within limits
  if (lines.length <= maxLines && totalBytes <= maxBytes) {
    return {
      content: output,
      truncated: false,
      totalLines,
      totalBytes,
    }
  }

  // Persist full output if requested
  let outputPath: string | undefined
  if (persistFull && workingDirectory) {
    outputPath = await persistOutput(output, workingDirectory, outputId)
  }

  // Truncate based on direction
  const truncatedLines: string[] = []
  let bytes = 0

  if (direction === 'head') {
    // Keep from start
    for (let i = 0; i < lines.length && i < maxLines; i++) {
      const lineBytes = new TextEncoder().encode(lines[i]).length + 1
      if (bytes + lineBytes > maxBytes) {
        break
      }
      truncatedLines.push(lines[i])
      bytes += lineBytes
    }
  } else {
    // Keep from end (tail)
    const startIndex = Math.max(0, lines.length - maxLines)
    for (let i = startIndex; i < lines.length; i++) {
      const lineBytes = new TextEncoder().encode(lines[i]).length + 1
      if (bytes + lineBytes > maxBytes) {
        // Remove from front of truncatedLines if over bytes
        while (truncatedLines.length > 0 && bytes + lineBytes > maxBytes) {
          const removed = truncatedLines.shift()!
          bytes -= new TextEncoder().encode(removed).length + 1
        }
      }
      truncatedLines.push(lines[i])
      bytes += lineBytes
    }
  }

  const removedLines = totalLines - truncatedLines.length
  const removedBytes = totalBytes - bytes

  // Build truncation notice
  let content = truncatedLines.join('\n')
  const notice = buildTruncationNotice(removedLines, removedBytes, direction, outputPath)
  content = direction === 'head' ? `${content}\n${notice}` : `${notice}\n${content}`

  return {
    content,
    truncated: true,
    removedLines,
    removedBytes,
    totalLines,
    totalBytes,
    outputPath,
  }
}

/**
 * Truncate for metadata streaming (smaller limit, synchronous)
 */
export function truncateForMetadata(
  output: string,
  maxBytes = TRUNCATION_LIMITS.MAX_METADATA_BYTES
): string {
  const bytes = new TextEncoder().encode(output)
  if (bytes.length <= maxBytes) {
    return output
  }

  // Truncate to maxBytes, keeping from end (most recent output)
  const decoder = new TextDecoder()
  return decoder.decode(bytes.slice(-maxBytes))
}

/**
 * Truncate a single line to max length
 */
export function truncateLine(line: string, maxLength = TRUNCATION_LIMITS.MAX_LINE_LENGTH): string {
  if (line.length <= maxLength) {
    return line
  }
  return `${line.slice(0, maxLength - 3)}...`
}

// ============================================================================
// Persistence
// ============================================================================

/**
 * Persist full output to file
 */
async function persistOutput(
  output: string,
  workingDirectory: string,
  outputId?: string
): Promise<string> {
  const fs = getPlatform().fs
  const id = outputId ?? `output-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`
  const dirPath = `${workingDirectory}/${OUTPUT_DIR}`
  const filePath = `${dirPath}/${id}.txt`

  try {
    // Ensure directory exists
    await fs.mkdir(dirPath)
  } catch {
    // Directory may already exist
  }

  await fs.writeFile(filePath, output)
  return filePath
}

/**
 * Build truncation notice message
 */
function buildTruncationNotice(
  removedLines: number,
  removedBytes: number,
  direction: 'head' | 'tail',
  outputPath?: string
): string {
  const parts: string[] = []

  parts.push(`\n... [Truncated: ${removedLines} lines, ${formatBytes(removedBytes)} removed]`)

  if (direction === 'tail') {
    parts.push('(showing end of output)')
  }

  if (outputPath) {
    parts.push(`Full output saved to: ${outputPath}`)
    parts.push('Use read_file with offset to view more.')
  }

  return parts.join('\n')
}

/**
 * Format bytes for display
 */
function formatBytes(bytes: number): string {
  if (bytes < 1024) {
    return `${bytes}B`
  }
  if (bytes < 1024 * 1024) {
    return `${(bytes / 1024).toFixed(1)}KB`
  }
  return `${(bytes / (1024 * 1024)).toFixed(1)}MB`
}

// ============================================================================
// Cleanup
// ============================================================================

/**
 * Clean up old output files
 *
 * @param workingDirectory - Project working directory
 * @param maxAgeMs - Maximum age in milliseconds (default: 7 days)
 */
export async function cleanupOutputFiles(
  workingDirectory: string,
  maxAgeMs = 7 * 24 * 60 * 60 * 1000
): Promise<{ deleted: number; errors: number }> {
  const fs = getPlatform().fs
  const dirPath = `${workingDirectory}/${OUTPUT_DIR}`
  const now = Date.now()
  let deleted = 0
  let errors = 0

  try {
    const files = await fs.readDir(dirPath)

    for (const fileName of files) {
      if (!fileName.endsWith('.txt')) continue

      try {
        const filePath = `${dirPath}/${fileName}`
        const stat = await fs.stat(filePath)
        const age = now - stat.mtime

        if (age > maxAgeMs) {
          await fs.remove(filePath)
          deleted++
        }
      } catch {
        errors++
      }
    }
  } catch {
    // Directory doesn't exist or can't be read
  }

  return { deleted, errors }
}
