/**
 * Patch Applier
 * Apply parsed patches to files
 */

import { getPlatform } from '../../platform.js'
import { similarity } from '../edit-replacers.js'
import { resolvePath } from '../utils.js'
import type { ParsedPatch, PatchChunk, PatchFile } from './parser.js'

// ============================================================================
// Types
// ============================================================================

/**
 * Result of applying a single file patch
 */
export interface FileApplyResult {
  path: string
  operation: string
  success: boolean
  error?: string
  /** Original content (for rollback) */
  originalContent?: string
  /** Content after changes */
  newContent?: string
}

/**
 * Result of applying entire patch
 */
export interface PatchApplyResult {
  success: boolean
  files: FileApplyResult[]
  /** Total number of operations */
  totalOperations: number
  /** Number of successful operations */
  successCount: number
  /** Number of failed operations */
  failureCount: number
  /** Overall error message if critical failure */
  error?: string
}

// ============================================================================
// Configuration
// ============================================================================

/** Minimum similarity for fuzzy context matching */
const CONTEXT_SIMILARITY_THRESHOLD = 0.6

/** Maximum lines to search for context match */
const MAX_CONTEXT_SEARCH_LINES = 50

// ============================================================================
// Applier Functions
// ============================================================================

/**
 * Apply a parsed patch to the filesystem
 *
 * @param patch - Parsed patch to apply
 * @param workingDirectory - Base directory for relative paths
 * @param dryRun - If true, validate without writing
 * @returns Apply result
 */
export async function applyPatch(
  patch: ParsedPatch,
  workingDirectory: string,
  dryRun = false
): Promise<PatchApplyResult> {
  const results: FileApplyResult[] = []
  let successCount = 0
  let failureCount = 0

  // Validate patch first
  if (patch.errors.length > 0) {
    return {
      success: false,
      files: [],
      totalOperations: patch.files.length,
      successCount: 0,
      failureCount: patch.files.length,
      error: `Patch validation failed: ${patch.errors.join('; ')}`,
    }
  }

  // Apply each file operation
  for (const file of patch.files) {
    const result = await applyFileOperation(file, workingDirectory, dryRun)
    results.push(result)

    if (result.success) {
      successCount++
    } else {
      failureCount++
    }
  }

  return {
    success: failureCount === 0,
    files: results,
    totalOperations: patch.files.length,
    successCount,
    failureCount,
  }
}

/**
 * Apply a single file operation
 */
async function applyFileOperation(
  file: PatchFile,
  workingDirectory: string,
  dryRun: boolean
): Promise<FileApplyResult> {
  const filePath = resolvePath(file.path, workingDirectory)

  try {
    switch (file.operation) {
      case 'add':
        return await applyAddOperation(file, filePath, dryRun)

      case 'update':
        return await applyUpdateOperation(file, filePath, dryRun)

      case 'delete':
        return await applyDeleteOperation(file, filePath, dryRun)

      case 'move':
        return await applyMoveOperation(file, filePath, workingDirectory, dryRun)

      default:
        return {
          path: file.path,
          operation: file.operation,
          success: false,
          error: `Unknown operation: ${file.operation}`,
        }
    }
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
    return {
      path: file.path,
      operation: file.operation,
      success: false,
      error: message,
    }
  }
}

/**
 * Apply add file operation
 */
async function applyAddOperation(
  file: PatchFile,
  filePath: string,
  dryRun: boolean
): Promise<FileApplyResult> {
  const { fs } = getPlatform()

  // Check if file already exists
  try {
    await fs.stat(filePath)
    return {
      path: file.path,
      operation: 'add',
      success: false,
      error: 'File already exists',
    }
  } catch {
    // File doesn't exist, good to proceed
  }

  // Build new content from added lines
  const newContent = file.chunks
    .flatMap((chunk) => chunk.lines.filter((l) => l.type === 'add').map((l) => l.content))
    .join('\n')

  if (!dryRun) {
    await fs.writeFile(filePath, newContent)
  }

  return {
    path: file.path,
    operation: 'add',
    success: true,
    newContent,
  }
}

/**
 * Apply update file operation with fuzzy context matching
 */
async function applyUpdateOperation(
  file: PatchFile,
  filePath: string,
  dryRun: boolean
): Promise<FileApplyResult> {
  const fs = getPlatform().fs

  // Read existing content
  let content: string
  try {
    content = await fs.readFile(filePath)
  } catch {
    return {
      path: file.path,
      operation: 'update',
      success: false,
      error: 'File not found',
    }
  }

  const originalContent = content

  // Apply each chunk
  for (const chunk of file.chunks) {
    const chunkResult = applyChunk(content, chunk)
    if (!chunkResult.success) {
      return {
        path: file.path,
        operation: 'update',
        success: false,
        error: chunkResult.error,
        originalContent,
      }
    }
    content = chunkResult.content
  }

  if (!dryRun) {
    await fs.writeFile(filePath, content)
  }

  return {
    path: file.path,
    operation: 'update',
    success: true,
    originalContent,
    newContent: content,
  }
}

/**
 * Apply delete file operation
 */
async function applyDeleteOperation(
  file: PatchFile,
  filePath: string,
  dryRun: boolean
): Promise<FileApplyResult> {
  const fs = getPlatform().fs

  // Check if file exists
  let originalContent: string | undefined
  try {
    originalContent = await fs.readFile(filePath)
  } catch {
    return {
      path: file.path,
      operation: 'delete',
      success: false,
      error: 'File not found',
    }
  }

  if (!dryRun) {
    await fs.remove(filePath)
  }

  return {
    path: file.path,
    operation: 'delete',
    success: true,
    originalContent,
  }
}

/**
 * Apply move file operation
 */
async function applyMoveOperation(
  file: PatchFile,
  filePath: string,
  workingDirectory: string,
  dryRun: boolean
): Promise<FileApplyResult> {
  const fs = getPlatform().fs

  if (!file.newPath) {
    return {
      path: file.path,
      operation: 'move',
      success: false,
      error: 'Move operation missing destination path',
    }
  }

  const newFilePath = resolvePath(file.newPath, workingDirectory)

  // Check source exists
  let originalContent: string
  try {
    originalContent = await fs.readFile(filePath)
  } catch {
    return {
      path: file.path,
      operation: 'move',
      success: false,
      error: 'Source file not found',
    }
  }

  // Check destination doesn't exist
  try {
    await fs.stat(newFilePath)
    return {
      path: file.path,
      operation: 'move',
      success: false,
      error: 'Destination file already exists',
    }
  } catch {
    // Good, destination doesn't exist
  }

  if (!dryRun) {
    await fs.writeFile(newFilePath, originalContent)
    await fs.remove(filePath)
  }

  return {
    path: file.path,
    operation: 'move',
    success: true,
    originalContent,
    newContent: originalContent,
  }
}

// ============================================================================
// Chunk Application with Fuzzy Matching
// ============================================================================

interface ChunkResult {
  success: boolean
  content: string
  error?: string
}

/**
 * Apply a single chunk to content using fuzzy context matching
 */
function applyChunk(content: string, chunk: PatchChunk): ChunkResult {
  const lines = content.split('\n')

  // Find the location to apply the chunk
  const location = findChunkLocation(lines, chunk)

  if (!location.found) {
    return {
      success: false,
      content,
      error: location.error || 'Could not find location to apply chunk',
    }
  }

  // Build the expected old content (context + deletions)
  const expectedOld: string[] = []
  for (const line of chunk.lines) {
    if (line.type === 'context' || line.type === 'delete') {
      expectedOld.push(line.content)
    }
  }

  // Verify the old content matches (with fuzzy matching)
  const actualOld = lines.slice(location.startLine, location.startLine + expectedOld.length)
  if (!contentMatches(expectedOld, actualOld)) {
    return {
      success: false,
      content,
      error: 'Content mismatch: expected content does not match actual file content',
    }
  }

  // Build new content
  const newLines: string[] = []
  for (const line of chunk.lines) {
    if (line.type === 'context' || line.type === 'add') {
      newLines.push(line.content)
    }
  }

  // Replace the lines
  const resultLines = [
    ...lines.slice(0, location.startLine),
    ...newLines,
    ...lines.slice(location.startLine + expectedOld.length),
  ]

  return {
    success: true,
    content: resultLines.join('\n'),
  }
}

interface LocationResult {
  found: boolean
  startLine: number
  error?: string
}

/**
 * Find where to apply a chunk using context matching
 */
function findChunkLocation(lines: string[], chunk: PatchChunk): LocationResult {
  // If we have a context line hint, try to find it first
  if (chunk.contextLine) {
    const contextIndex = findContextLine(lines, chunk.contextLine)
    if (contextIndex !== -1) {
      return { found: true, startLine: contextIndex }
    }
  }

  // Get the first context or delete line to search for
  const firstOldLine = chunk.lines.find((l) => l.type === 'context' || l.type === 'delete')
  if (!firstOldLine) {
    // No context, might be pure addition at a specific location
    // For now, fail if we can't find context
    return { found: false, startLine: 0, error: 'No context or delete lines to locate chunk' }
  }

  // Search for the first line with fuzzy matching
  for (let i = 0; i < lines.length && i < MAX_CONTEXT_SEARCH_LINES * 10; i++) {
    const sim = similarity(lines[i].trim(), firstOldLine.content.trim())
    if (sim >= CONTEXT_SIMILARITY_THRESHOLD) {
      // Verify subsequent lines match
      const oldLines = chunk.lines
        .filter((l) => l.type === 'context' || l.type === 'delete')
        .map((l) => l.content)

      if (contentMatches(oldLines, lines.slice(i, i + oldLines.length))) {
        return { found: true, startLine: i }
      }
    }
  }

  return { found: false, startLine: 0, error: 'Could not locate chunk context in file' }
}

/**
 * Find a context line in the file
 */
function findContextLine(lines: string[], contextLine: string): number {
  const normalized = contextLine.trim()

  // Exact match first
  for (let i = 0; i < lines.length; i++) {
    if (lines[i].trim() === normalized) {
      return i
    }
  }

  // Fuzzy match
  let bestMatch = -1
  let bestSimilarity = 0

  for (let i = 0; i < lines.length && i < MAX_CONTEXT_SEARCH_LINES * 10; i++) {
    const sim = similarity(lines[i].trim(), normalized)
    if (sim > bestSimilarity && sim >= CONTEXT_SIMILARITY_THRESHOLD) {
      bestSimilarity = sim
      bestMatch = i
    }
  }

  return bestMatch
}

/**
 * Check if expected and actual content match (with fuzzy matching)
 */
function contentMatches(expected: string[], actual: string[]): boolean {
  if (expected.length !== actual.length) {
    return false
  }

  for (let i = 0; i < expected.length; i++) {
    const sim = similarity(expected[i].trim(), actual[i].trim())
    if (sim < CONTEXT_SIMILARITY_THRESHOLD) {
      return false
    }
  }

  return true
}
