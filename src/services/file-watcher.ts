/**
 * File Watcher Service
 *
 * Watches project files for AI comment patterns and triggers chat actions.
 * Inspired by Aider's watch mode (AI! = execute, AI? = question).
 *
 * Patterns detected:
 *   // AI! <instruction>      → execute instruction
 *   // AI? <question>         → answer question
 *   /* AI! <instruction> *​/   → block comment variant
 *   # AI! <instruction>       → Python/Ruby/Shell
 */

import { createSignal } from 'solid-js'
import { logDebug, logInfo, logWarn } from './logger'

// ============================================================================
// Types
// ============================================================================

export interface AIComment {
  filePath: string
  lineNumber: number
  type: 'execute' | 'question'
  content: string
  /** Surrounding lines for context */
  context: string
}

export type AICommentCallback = (comment: AIComment) => void

// ============================================================================
// Comment Detection
// ============================================================================

interface CommentPattern {
  regex: RegExp
  type: 'execute' | 'question'
}

/** Single-line and block comment patterns for AI directives */
const AI_PATTERNS: CommentPattern[] = [
  // // AI! instruction
  { regex: /\/\/\s*AI!\s+(.+)$/, type: 'execute' },
  // // AI? question
  { regex: /\/\/\s*AI\?\s+(.+)$/, type: 'question' },
  // # AI! instruction (Python, Ruby, Shell, YAML)
  { regex: /#\s*AI!\s+(.+)$/, type: 'execute' },
  // # AI? question
  { regex: /#\s*AI\?\s+(.+)$/, type: 'question' },
  // -- AI! instruction (SQL, Lua)
  { regex: /--\s*AI!\s+(.+)$/, type: 'execute' },
  // -- AI? question
  { regex: /--\s*AI\?\s+(.+)$/, type: 'question' },
]

/** Context lines to include before and after an AI comment */
const CONTEXT_LINES = 3

/**
 * Scan file content for AI comment patterns.
 * Returns all detected comments with line numbers and context.
 */
function scanForComments(filePath: string, content: string): AIComment[] {
  const comments: AIComment[] = []
  const lines = content.split('\n')

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]

    for (const pattern of AI_PATTERNS) {
      const match = pattern.regex.exec(line)
      if (match?.[1]) {
        const commentContent = match[1].trim()
        if (!commentContent) continue

        const start = Math.max(0, i - CONTEXT_LINES)
        const end = Math.min(lines.length, i + CONTEXT_LINES + 1)
        const context = lines.slice(start, end).join('\n')

        comments.push({
          filePath,
          lineNumber: i + 1,
          type: pattern.type,
          content: commentContent,
          context,
        })
        break // one match per line
      }
    }
  }

  return comments
}

// ============================================================================
// File Filtering
// ============================================================================

/** Directories to always skip */
const IGNORE_DIRS = [
  'node_modules',
  '.git',
  'dist',
  'build',
  '.next',
  '.cache',
  '__pycache__',
  'target',
  '.turbo',
  '.output',
  'coverage',
]

/** File extensions to scan (source code only) */
const SCANNABLE_EXTENSIONS = new Set([
  '.ts',
  '.tsx',
  '.js',
  '.jsx',
  '.py',
  '.rs',
  '.go',
  '.java',
  '.c',
  '.cpp',
  '.h',
  '.hpp',
  '.rb',
  '.sh',
  '.bash',
  '.lua',
  '.sql',
  '.css',
  '.scss',
  '.yaml',
  '.yml',
  '.toml',
  '.md',
  '.txt',
  '.vue',
  '.svelte',
  '.astro',
])

function shouldIgnorePath(filePath: string): boolean {
  const normalized = filePath.replace(/\\/g, '/')
  for (const dir of IGNORE_DIRS) {
    if (normalized.includes(`/${dir}/`)) return true
  }
  return false
}

function isScannableFile(filePath: string): boolean {
  const ext = filePath.slice(filePath.lastIndexOf('.')).toLowerCase()
  return SCANNABLE_EXTENSIONS.has(ext)
}

// ============================================================================
// Watcher State
// ============================================================================

/** Track processed comments to avoid re-triggering */
const processedHashes = new Set<string>()

function commentKey(c: AIComment): string {
  return `${c.filePath}:${c.lineNumber}:${c.content}`
}

const [isWatching, setIsWatching] = createSignal(false)
const [pendingComments, setPendingComments] = createSignal<AIComment[]>([])

let unwatchFn: (() => void) | null = null

// ============================================================================
// Public API
// ============================================================================

/**
 * Start watching a project directory for AI comments.
 *
 * Uses Tauri FS watch with 500ms debounce. On file change,
 * scans for AI comment patterns and triggers the callback
 * for new (not previously processed) comments.
 */
export async function startFileWatcher(
  projectDir: string,
  onComment: AICommentCallback
): Promise<void> {
  await stopFileWatcher()

  try {
    const fs = await import('@tauri-apps/plugin-fs')

    const unwatch = await fs.watch(
      projectDir,
      async (event) => {
        if (!event.paths?.length) return

        for (const filePath of event.paths) {
          if (shouldIgnorePath(filePath)) continue
          if (!isScannableFile(filePath)) continue

          try {
            const content = await fs.readTextFile(filePath)
            const comments = scanForComments(filePath, content)

            const newComments: AIComment[] = []
            for (const comment of comments) {
              const key = commentKey(comment)
              if (!processedHashes.has(key)) {
                processedHashes.add(key)
                newComments.push(comment)
                onComment(comment)
              } else {
                logDebug('file-watcher', 'Dedup comment', {
                  filePath: comment.filePath,
                  lineNumber: comment.lineNumber,
                })
              }
            }

            if (newComments.length > 0) {
              setPendingComments((prev) => [...prev, ...newComments])
              logInfo('file-watcher', 'AI comment detected', {
                count: newComments.length,
                filePath,
              })
            }
          } catch {
            // File might be deleted, binary, or locked — skip silently
          }
        }
      },
      { recursive: true, delayMs: 500 }
    )

    unwatchFn = unwatch as () => void
    setIsWatching(true)
    logInfo('file-watcher', 'Watching started', { projectDir })
  } catch (err) {
    logWarn('file-watcher', 'Failed to start', err)
  }
}

/**
 * Stop watching and clear state.
 */
export async function stopFileWatcher(): Promise<void> {
  if (unwatchFn) {
    unwatchFn()
    unwatchFn = null
  }
  processedHashes.clear()
  setPendingComments([])
  setIsWatching(false)
  logInfo('file-watcher', 'Watching stopped')
}

/**
 * Clear a processed comment (e.g., after user acts on it).
 */
export function dismissComment(comment: AIComment): void {
  setPendingComments((prev) => prev.filter((c) => commentKey(c) !== commentKey(comment)))
}

/**
 * Reactive accessors for UI binding.
 */
export function useFileWatcher() {
  return {
    isWatching,
    pendingComments,
    start: startFileWatcher,
    stop: stopFileWatcher,
    dismiss: dismissComment,
  }
}
