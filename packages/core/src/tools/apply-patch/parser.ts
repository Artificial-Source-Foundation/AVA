/**
 * Patch Parser
 * Parse unified diff format patches
 */

// ============================================================================
// Types
// ============================================================================

/**
 * Operation types for patch chunks
 */
export type PatchOperation = 'add' | 'update' | 'delete' | 'move'

/**
 * A single line change within a chunk
 */
export interface PatchLine {
  type: 'context' | 'add' | 'delete'
  content: string
  lineNumber?: number
}

/**
 * A chunk of changes within a file
 */
export interface PatchChunk {
  /** Context line that precedes this chunk */
  contextLine?: string
  /** Lines in this chunk */
  lines: PatchLine[]
  /** Original line range (for context) */
  originalRange?: { start: number; count: number }
  /** New line range (for context) */
  newRange?: { start: number; count: number }
}

/**
 * Changes for a single file
 */
export interface PatchFile {
  /** Operation type */
  operation: PatchOperation
  /** File path (relative or absolute) */
  path: string
  /** New path for move operations */
  newPath?: string
  /** Chunks of changes */
  chunks: PatchChunk[]
}

/**
 * Parsed patch containing multiple file changes
 */
export interface ParsedPatch {
  /** Files to modify */
  files: PatchFile[]
  /** Parse errors, if any */
  errors: string[]
}

// ============================================================================
// Parser
// ============================================================================

/**
 * Parse a unified diff patch
 *
 * Format:
 * ```
 * *** Begin Patch
 * *** Add File: path/to/new.txt
 * +new content
 * +more content
 *
 * *** Update File: path/to/existing.txt
 * @@ context line @@
 * -old line
 * +new line
 *
 * *** Delete File: path/to/remove.txt
 *
 * *** Move File: old/path.txt -> new/path.txt
 * *** End Patch
 * ```
 *
 * @param patch - Raw patch content
 * @returns Parsed patch
 */
export function parsePatch(patch: string): ParsedPatch {
  const files: PatchFile[] = []
  const errors: string[] = []
  const lines = patch.split('\n')

  let inPatch = false
  let currentFile: PatchFile | null = null
  let currentChunk: PatchChunk | null = null

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]

    // Start of patch
    if (line.trim() === '*** Begin Patch') {
      inPatch = true
      continue
    }

    // End of patch
    if (line.trim() === '*** End Patch') {
      if (currentChunk && currentFile) {
        currentFile.chunks.push(currentChunk)
      }
      if (currentFile) {
        files.push(currentFile)
      }
      inPatch = false
      currentFile = null
      currentChunk = null
      continue
    }

    if (!inPatch) {
      continue
    }

    // File operations
    const addMatch = line.match(/^\*\*\*\s+Add\s+File:\s*(.+)$/i)
    if (addMatch) {
      if (currentChunk && currentFile) {
        currentFile.chunks.push(currentChunk)
      }
      if (currentFile) {
        files.push(currentFile)
      }
      currentFile = {
        operation: 'add',
        path: addMatch[1].trim(),
        chunks: [],
      }
      currentChunk = { lines: [] }
      continue
    }

    const updateMatch = line.match(/^\*\*\*\s+Update\s+File:\s*(.+)$/i)
    if (updateMatch) {
      if (currentChunk && currentFile) {
        currentFile.chunks.push(currentChunk)
      }
      if (currentFile) {
        files.push(currentFile)
      }
      currentFile = {
        operation: 'update',
        path: updateMatch[1].trim(),
        chunks: [],
      }
      currentChunk = null
      continue
    }

    const deleteMatch = line.match(/^\*\*\*\s+Delete\s+File:\s*(.+)$/i)
    if (deleteMatch) {
      if (currentChunk && currentFile) {
        currentFile.chunks.push(currentChunk)
      }
      if (currentFile) {
        files.push(currentFile)
      }
      currentFile = {
        operation: 'delete',
        path: deleteMatch[1].trim(),
        chunks: [],
      }
      currentChunk = null
      continue
    }

    const moveMatch = line.match(/^\*\*\*\s+Move\s+File:\s*(.+)\s*->\s*(.+)$/i)
    if (moveMatch) {
      if (currentChunk && currentFile) {
        currentFile.chunks.push(currentChunk)
      }
      if (currentFile) {
        files.push(currentFile)
      }
      currentFile = {
        operation: 'move',
        path: moveMatch[1].trim(),
        newPath: moveMatch[2].trim(),
        chunks: [],
      }
      currentChunk = null
      continue
    }

    // Context line (starts a new chunk for update operations)
    const contextMatch = line.match(/^@@\s*(.+?)\s*@@/)
    if (contextMatch) {
      if (currentChunk && currentFile) {
        currentFile.chunks.push(currentChunk)
      }
      currentChunk = {
        contextLine: contextMatch[1].trim(),
        lines: [],
      }
      continue
    }

    // Skip empty lines between files
    if (!currentFile) {
      continue
    }

    // Skip empty lines at the start of chunks
    if (!currentChunk && line.trim() === '') {
      continue
    }

    // Start a chunk if we don't have one
    if (!currentChunk) {
      currentChunk = { lines: [] }
    }

    // Parse diff lines
    if (line.startsWith('+')) {
      currentChunk.lines.push({
        type: 'add',
        content: line.slice(1),
      })
    } else if (line.startsWith('-')) {
      currentChunk.lines.push({
        type: 'delete',
        content: line.slice(1),
      })
    } else if (line.startsWith(' ')) {
      currentChunk.lines.push({
        type: 'context',
        content: line.slice(1),
      })
    } else if (line.trim() === '') {
      // Empty line in context (preserve it)
      currentChunk.lines.push({
        type: 'context',
        content: '',
      })
    } else {
      // Unknown line format - treat as context
      currentChunk.lines.push({
        type: 'context',
        content: line,
      })
    }
  }

  // Handle unclosed patch
  if (inPatch) {
    errors.push('Patch was not properly closed with "*** End Patch"')
    if (currentChunk && currentFile) {
      currentFile.chunks.push(currentChunk)
    }
    if (currentFile) {
      files.push(currentFile)
    }
  }

  return { files, errors }
}

/**
 * Validate a parsed patch
 *
 * @param patch - Parsed patch to validate
 * @returns Array of validation errors
 */
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
