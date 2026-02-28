/**
 * Patch Applier — apply parsed patches to files.
 *
 * Adapted from packages/core/src/tools/apply-patch/applier.ts.
 * Uses @ava/core-v2 platform + similarity exports.
 */

import { getPlatform } from '@ava/core-v2/platform'
import { resolvePath, similarity } from '@ava/core-v2/tools'
import type { ParsedPatch, PatchChunk, PatchFile } from './parser.js'

export interface FileApplyResult {
  path: string
  operation: string
  success: boolean
  error?: string
  originalContent?: string
  newContent?: string
}

export interface PatchApplyResult {
  success: boolean
  files: FileApplyResult[]
  totalOperations: number
  successCount: number
  failureCount: number
  error?: string
}

const CONTEXT_SIMILARITY_THRESHOLD = 0.6
const MAX_CONTEXT_SEARCH_LINES = 500

export async function applyPatch(
  patch: ParsedPatch,
  workingDirectory: string,
  dryRun = false
): Promise<PatchApplyResult> {
  const results: FileApplyResult[] = []
  let successCount = 0
  let failureCount = 0

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
    return { path: file.path, operation: file.operation, success: false, error: message }
  }
}

async function applyAddOperation(
  file: PatchFile,
  filePath: string,
  dryRun: boolean
): Promise<FileApplyResult> {
  const { fs } = getPlatform()

  try {
    await fs.stat(filePath)
    return { path: file.path, operation: 'add', success: false, error: 'File already exists' }
  } catch {
    // File doesn't exist, good
  }

  const newContent = file.chunks
    .flatMap((chunk) => chunk.lines.filter((l) => l.type === 'add').map((l) => l.content))
    .join('\n')

  if (!dryRun) {
    await fs.writeFile(filePath, newContent)
  }

  return { path: file.path, operation: 'add', success: true, newContent }
}

async function applyUpdateOperation(
  file: PatchFile,
  filePath: string,
  dryRun: boolean
): Promise<FileApplyResult> {
  const fs = getPlatform().fs

  let content: string
  try {
    content = await fs.readFile(filePath)
  } catch {
    return { path: file.path, operation: 'update', success: false, error: 'File not found' }
  }

  const originalContent = content

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

async function applyDeleteOperation(
  file: PatchFile,
  filePath: string,
  dryRun: boolean
): Promise<FileApplyResult> {
  const fs = getPlatform().fs

  let originalContent: string | undefined
  try {
    originalContent = await fs.readFile(filePath)
  } catch {
    return { path: file.path, operation: 'delete', success: false, error: 'File not found' }
  }

  if (!dryRun) {
    await fs.remove(filePath)
  }

  return { path: file.path, operation: 'delete', success: true, originalContent }
}

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

  let originalContent: string
  try {
    originalContent = await fs.readFile(filePath)
  } catch {
    return { path: file.path, operation: 'move', success: false, error: 'Source file not found' }
  }

  try {
    await fs.stat(newFilePath)
    return {
      path: file.path,
      operation: 'move',
      success: false,
      error: 'Destination file already exists',
    }
  } catch {
    // Good
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

interface ChunkResult {
  success: boolean
  content: string
  error?: string
}

function applyChunk(content: string, chunk: PatchChunk): ChunkResult {
  const lines = content.split('\n')
  const location = findChunkLocation(lines, chunk)

  if (!location.found) {
    return {
      success: false,
      content,
      error: location.error || 'Could not find location to apply chunk',
    }
  }

  const expectedOld: string[] = []
  for (const line of chunk.lines) {
    if (line.type === 'context' || line.type === 'delete') {
      expectedOld.push(line.content)
    }
  }

  const actualOld = lines.slice(location.startLine, location.startLine + expectedOld.length)
  if (!contentMatches(expectedOld, actualOld)) {
    return {
      success: false,
      content,
      error: 'Content mismatch: expected content does not match actual file content',
    }
  }

  const newLines: string[] = []
  for (const line of chunk.lines) {
    if (line.type === 'context' || line.type === 'add') {
      newLines.push(line.content)
    }
  }

  const resultLines = [
    ...lines.slice(0, location.startLine),
    ...newLines,
    ...lines.slice(location.startLine + expectedOld.length),
  ]

  return { success: true, content: resultLines.join('\n') }
}

interface LocationResult {
  found: boolean
  startLine: number
  error?: string
}

function findChunkLocation(lines: string[], chunk: PatchChunk): LocationResult {
  if (chunk.contextLine) {
    const contextIndex = findContextLine(lines, chunk.contextLine)
    if (contextIndex !== -1) {
      return { found: true, startLine: contextIndex }
    }
  }

  const firstOldLine = chunk.lines.find((l) => l.type === 'context' || l.type === 'delete')
  if (!firstOldLine) {
    return { found: false, startLine: 0, error: 'No context or delete lines to locate chunk' }
  }

  for (let i = 0; i < lines.length && i < MAX_CONTEXT_SEARCH_LINES; i++) {
    const currentLine = lines[i]
    if (currentLine === undefined) continue
    const sim = similarity(currentLine.trim(), firstOldLine.content.trim())
    if (sim >= CONTEXT_SIMILARITY_THRESHOLD) {
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

function findContextLine(lines: string[], contextLine: string): number {
  const normalized = contextLine.trim()

  for (let i = 0; i < lines.length; i++) {
    const currentLine = lines[i]
    if (currentLine !== undefined && currentLine.trim() === normalized) return i
  }

  let bestMatch = -1
  let bestSimilarity = 0

  for (let i = 0; i < lines.length && i < MAX_CONTEXT_SEARCH_LINES; i++) {
    const currentLine = lines[i]
    if (currentLine === undefined) continue
    const sim = similarity(currentLine.trim(), normalized)
    if (sim > bestSimilarity && sim >= CONTEXT_SIMILARITY_THRESHOLD) {
      bestSimilarity = sim
      bestMatch = i
    }
  }

  return bestMatch
}

function contentMatches(expected: string[], actual: string[]): boolean {
  if (expected.length !== actual.length) return false

  for (let i = 0; i < expected.length; i++) {
    const exp = expected[i]
    const act = actual[i]
    if (exp === undefined || act === undefined) return false
    const sim = similarity(exp.trim(), act.trim())
    if (sim < CONTEXT_SIMILARITY_THRESHOLD) return false
  }

  return true
}
