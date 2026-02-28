/**
 * Patch Parser — parse unified diff format patches.
 *
 * Copied from packages/core/src/tools/apply-patch/parser.ts.
 * Pure parsing logic, zero external deps.
 */

export type PatchOperation = 'add' | 'update' | 'delete' | 'move'

export interface PatchLine {
  type: 'context' | 'add' | 'delete'
  content: string
  lineNumber?: number
}

export interface PatchChunk {
  contextLine?: string
  lines: PatchLine[]
  originalRange?: { start: number; count: number }
  newRange?: { start: number; count: number }
}

export interface PatchFile {
  operation: PatchOperation
  path: string
  newPath?: string
  chunks: PatchChunk[]
}

export interface ParsedPatch {
  files: PatchFile[]
  errors: string[]
}

export function parsePatch(patch: string): ParsedPatch {
  const files: PatchFile[] = []
  const errors: string[] = []
  const lines = patch.split('\n')

  let inPatch = false
  let currentFile: PatchFile | null = null
  let currentChunk: PatchChunk | null = null

  for (const line of lines) {
    if (line.trim() === '*** Begin Patch') {
      inPatch = true
      continue
    }

    if (line.trim() === '*** End Patch') {
      if (currentChunk && currentFile) currentFile.chunks.push(currentChunk)
      if (currentFile) files.push(currentFile)
      inPatch = false
      currentFile = null
      currentChunk = null
      continue
    }

    if (!inPatch) continue

    const addMatch = line.match(/^\*\*\*\s+Add\s+File:\s*(.+)$/i)
    if (addMatch) {
      if (currentChunk && currentFile) currentFile.chunks.push(currentChunk)
      if (currentFile) files.push(currentFile)
      currentFile = { operation: 'add', path: (addMatch[1] ?? '').trim(), chunks: [] }
      currentChunk = { lines: [] }
      continue
    }

    const updateMatch = line.match(/^\*\*\*\s+Update\s+File:\s*(.+)$/i)
    if (updateMatch) {
      if (currentChunk && currentFile) currentFile.chunks.push(currentChunk)
      if (currentFile) files.push(currentFile)
      currentFile = { operation: 'update', path: (updateMatch[1] ?? '').trim(), chunks: [] }
      currentChunk = null
      continue
    }

    const deleteMatch = line.match(/^\*\*\*\s+Delete\s+File:\s*(.+)$/i)
    if (deleteMatch) {
      if (currentChunk && currentFile) currentFile.chunks.push(currentChunk)
      if (currentFile) files.push(currentFile)
      currentFile = { operation: 'delete', path: (deleteMatch[1] ?? '').trim(), chunks: [] }
      currentChunk = null
      continue
    }

    const moveMatch = line.match(/^\*\*\*\s+Move\s+File:\s*(.+)\s*->\s*(.+)$/i)
    if (moveMatch) {
      if (currentChunk && currentFile) currentFile.chunks.push(currentChunk)
      if (currentFile) files.push(currentFile)
      currentFile = {
        operation: 'move',
        path: (moveMatch[1] ?? '').trim(),
        newPath: (moveMatch[2] ?? '').trim(),
        chunks: [],
      }
      currentChunk = null
      continue
    }

    const contextMatch = line.match(/^@@\s*(.+?)\s*@@/)
    if (contextMatch) {
      if (currentChunk && currentFile) currentFile.chunks.push(currentChunk)
      currentChunk = { contextLine: (contextMatch[1] ?? '').trim(), lines: [] }
      continue
    }

    if (!currentFile) continue
    if (!currentChunk && line.trim() === '') continue

    if (!currentChunk) {
      currentChunk = { lines: [] }
    }

    if (line.startsWith('+')) {
      currentChunk.lines.push({ type: 'add', content: line.slice(1) })
    } else if (line.startsWith('-')) {
      currentChunk.lines.push({ type: 'delete', content: line.slice(1) })
    } else if (line.startsWith(' ')) {
      currentChunk.lines.push({ type: 'context', content: line.slice(1) })
    } else if (line.trim() === '') {
      currentChunk.lines.push({ type: 'context', content: '' })
    } else {
      currentChunk.lines.push({ type: 'context', content: line })
    }
  }

  if (inPatch) {
    errors.push('Patch was not properly closed with "*** End Patch"')
    if (currentChunk && currentFile) currentFile.chunks.push(currentChunk)
    if (currentFile) files.push(currentFile)
  }

  return { files, errors }
}

export function validatePatch(patch: ParsedPatch): string[] {
  const errors = [...patch.errors]

  for (const file of patch.files) {
    if (!file.path) {
      errors.push('File operation missing path')
    }
    if (file.operation === 'move' && !file.newPath) {
      errors.push(`Move operation for "${file.path}" missing destination path`)
    }
    if (file.operation === 'update' && file.chunks.length === 0) {
      errors.push(`Update operation for "${file.path}" has no changes`)
    }
  }

  return errors
}
