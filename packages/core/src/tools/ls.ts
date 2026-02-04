/**
 * Ls Tool
 * List directory contents in a tree-view format
 *
 * Based on OpenCode's ls.ts pattern
 */

import { getPlatform } from '../platform.js'
import { ToolError, ToolErrorType } from './errors.js'
import type { Tool, ToolContext, ToolResult } from './types.js'
import { resolvePath, shouldSkipDirectory } from './utils.js'

// ============================================================================
// Types
// ============================================================================

interface LsParams {
  /** Directory path to list (defaults to working directory) */
  path?: string
  /** Additional patterns to ignore */
  ignore?: string[]
  /** List recursively (default: true) */
  recursive?: boolean
  /** Maximum files to return (default: 100) */
  maxFiles?: number
}

interface FileEntry {
  name: string
  path: string
  isDirectory: boolean
  children?: FileEntry[]
}

// ============================================================================
// Constants
// ============================================================================

/** Default patterns to ignore */
const DEFAULT_IGNORE_PATTERNS = [
  'node_modules',
  '.git',
  '.svn',
  '.hg',
  '__pycache__',
  '.pytest_cache',
  '.mypy_cache',
  '.tox',
  '.nox',
  '.eggs',
  '*.egg-info',
  'dist',
  'build',
  'target',
  '.next',
  '.nuxt',
  '.output',
  '.vercel',
  '.netlify',
  'coverage',
  '.nyc_output',
  '.cache',
  '.parcel-cache',
  '.turbo',
  'vendor',
  '.bundle',
  'tmp',
  'temp',
  '.temp',
  '.tmp',
  'logs',
  '*.log',
  '.DS_Store',
  'Thumbs.db',
  '.idea',
  '.vscode',
  '*.swp',
  '*.swo',
  '*~',
]

/** Default max files to return */
const DEFAULT_MAX_FILES = 100

/** Tree drawing characters */
const TREE_CHARS = {
  branch: '├── ',
  lastBranch: '└── ',
  vertical: '│   ',
  space: '    ',
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Check if a name matches any ignore pattern
 */
function shouldIgnore(name: string, patterns: string[]): boolean {
  // Check built-in skip (node_modules, .git, etc.)
  if (shouldSkipDirectory(name)) {
    return true
  }

  for (const pattern of patterns) {
    // Simple glob matching
    if (pattern.startsWith('*.')) {
      // Extension pattern
      const ext = pattern.slice(1)
      if (name.endsWith(ext)) {
        return true
      }
    } else if (pattern.endsWith('*')) {
      // Prefix pattern
      const prefix = pattern.slice(0, -1)
      if (name.startsWith(prefix)) {
        return true
      }
    } else if (name === pattern) {
      // Exact match
      return true
    }
  }

  return false
}

/**
 * Recursively list directory contents
 */
async function listDirectory(
  dirPath: string,
  ignorePatterns: string[],
  recursive: boolean,
  maxFiles: number,
  currentCount: { value: number },
  signal: AbortSignal
): Promise<FileEntry[]> {
  const fs = getPlatform().fs

  if (signal.aborted || currentCount.value >= maxFiles) {
    return []
  }

  const entries: FileEntry[] = []

  try {
    const items = await fs.readDirWithTypes(dirPath)

    // Sort: directories first, then alphabetically
    items.sort((a, b) => {
      if (a.isDirectory && !b.isDirectory) return -1
      if (!a.isDirectory && b.isDirectory) return 1
      return a.name.localeCompare(b.name)
    })

    for (const item of items) {
      if (signal.aborted || currentCount.value >= maxFiles) {
        break
      }

      if (shouldIgnore(item.name, ignorePatterns)) {
        continue
      }

      const fullPath = `${dirPath}/${item.name}`
      const entry: FileEntry = {
        name: item.name,
        path: fullPath,
        isDirectory: item.isDirectory,
      }

      currentCount.value++

      if (item.isDirectory && recursive) {
        entry.children = await listDirectory(
          fullPath,
          ignorePatterns,
          recursive,
          maxFiles,
          currentCount,
          signal
        )
      }

      entries.push(entry)
    }
  } catch {
    // Permission denied or other error - skip this directory
  }

  return entries
}

/**
 * Render file tree as string
 */
function renderTree(entries: FileEntry[], prefix = ''): string[] {
  const lines: string[] = []

  for (let i = 0; i < entries.length; i++) {
    const entry = entries[i]
    const isLastEntry = i === entries.length - 1
    const connector = isLastEntry ? TREE_CHARS.lastBranch : TREE_CHARS.branch
    const childPrefix = prefix + (isLastEntry ? TREE_CHARS.space : TREE_CHARS.vertical)

    // Add directory indicator
    const displayName = entry.isDirectory ? `${entry.name}/` : entry.name
    lines.push(`${prefix}${connector}${displayName}`)

    // Render children if directory
    if (entry.children && entry.children.length > 0) {
      lines.push(...renderTree(entry.children, childPrefix))
    }
  }

  return lines
}

// ============================================================================
// Tool Implementation
// ============================================================================

export const lsTool: Tool<LsParams> = {
  definition: {
    name: 'ls',
    description: `List directory contents in a tree-view format.

Features:
- Recursively lists files and directories
- Ignores common build artifacts and dependencies (node_modules, .git, dist, etc.)
- Returns up to 100 files by default
- Tree-view output for easy reading

Use this tool to explore directory structure before reading specific files.`,
    input_schema: {
      type: 'object',
      properties: {
        path: {
          type: 'string',
          description: 'Directory path to list (defaults to working directory)',
        },
        ignore: {
          type: 'array',
          items: { type: 'string' },
          description: 'Additional patterns to ignore (e.g., ["*.test.ts", "temp"])',
        },
        recursive: {
          type: 'boolean',
          description: 'List recursively (default: true)',
        },
        maxFiles: {
          type: 'number',
          description: 'Maximum files to return (default: 100)',
        },
      },
      required: [],
    },
  },

  validate(params: unknown): LsParams {
    if (typeof params !== 'object' || params === null) {
      throw new ToolError('Invalid params: expected object', ToolErrorType.INVALID_PARAMS, 'ls')
    }

    const { path, ignore, recursive, maxFiles } = params as Record<string, unknown>

    if (path !== undefined && typeof path !== 'string') {
      throw new ToolError('Invalid path: must be string', ToolErrorType.INVALID_PARAMS, 'ls')
    }

    if (ignore !== undefined) {
      if (!Array.isArray(ignore)) {
        throw new ToolError('Invalid ignore: must be array', ToolErrorType.INVALID_PARAMS, 'ls')
      }
      if (!ignore.every((p) => typeof p === 'string')) {
        throw new ToolError(
          'Invalid ignore: all items must be strings',
          ToolErrorType.INVALID_PARAMS,
          'ls'
        )
      }
    }

    if (recursive !== undefined && typeof recursive !== 'boolean') {
      throw new ToolError('Invalid recursive: must be boolean', ToolErrorType.INVALID_PARAMS, 'ls')
    }

    if (maxFiles !== undefined) {
      if (typeof maxFiles !== 'number' || maxFiles <= 0 || !Number.isInteger(maxFiles)) {
        throw new ToolError(
          'Invalid maxFiles: must be positive integer',
          ToolErrorType.INVALID_PARAMS,
          'ls'
        )
      }
    }

    return {
      path: typeof path === 'string' ? path.trim() : undefined,
      ignore: ignore as string[] | undefined,
      recursive: recursive as boolean | undefined,
      maxFiles: maxFiles as number | undefined,
    }
  },

  async execute(params: LsParams, ctx: ToolContext): Promise<ToolResult> {
    const fs = getPlatform().fs

    // Resolve directory path
    const dirPath = params.path
      ? resolvePath(params.path, ctx.workingDirectory)
      : ctx.workingDirectory

    const recursive = params.recursive !== false
    const maxFiles = params.maxFiles ?? DEFAULT_MAX_FILES
    const ignorePatterns = [...DEFAULT_IGNORE_PATTERNS, ...(params.ignore ?? [])]

    // Check abort signal
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    try {
      // Check directory exists
      const stat = await fs.stat(dirPath)
      if (!stat.isDirectory) {
        return {
          success: false,
          output: `Path is a file, not a directory: ${dirPath}`,
          error: ToolErrorType.INVALID_PARAMS,
        }
      }

      // List directory contents
      const counter = { value: 0 }
      const entries = await listDirectory(
        dirPath,
        ignorePatterns,
        recursive,
        maxFiles,
        counter,
        ctx.signal
      )

      if (ctx.signal.aborted) {
        return {
          success: false,
          output: 'Operation was cancelled',
          error: ToolErrorType.EXECUTION_ABORTED,
        }
      }

      // Render tree
      const dirName = dirPath.split('/').pop() ?? dirPath
      const treeLines = [`${dirName}/`, ...renderTree(entries)]

      let output = treeLines.join('\n')

      // Add truncation notice if needed
      if (counter.value >= maxFiles) {
        output += `\n\n... (truncated at ${maxFiles} files. Use maxFiles parameter to increase limit)`
      }

      // Stream metadata if available
      if (ctx.metadata) {
        ctx.metadata({
          title: `Listed ${dirPath}`,
          metadata: {
            fileCount: counter.value,
            truncated: counter.value >= maxFiles,
          },
        })
      }

      return {
        success: true,
        output,
        metadata: {
          path: dirPath,
          fileCount: counter.value,
          truncated: counter.value >= maxFiles,
          recursive,
        },
        locations: [{ path: dirPath, type: 'read' }],
      }
    } catch (err) {
      // Handle directory not found
      if (err instanceof Error && err.message.includes('ENOENT')) {
        return {
          success: false,
          output: `Directory not found: ${dirPath}`,
          error: ToolErrorType.FILE_NOT_FOUND,
        }
      }

      // Handle permission errors
      if (err instanceof Error && err.message.includes('EACCES')) {
        return {
          success: false,
          output: `Permission denied: ${dirPath}`,
          error: ToolErrorType.PERMISSION_DENIED,
        }
      }

      const message = err instanceof Error ? err.message : String(err)
      return {
        success: false,
        output: `Error listing directory: ${message}`,
        error: ToolErrorType.UNKNOWN,
      }
    }
  },
}
