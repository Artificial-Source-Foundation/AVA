/**
 * Git Utilities
 * Helper functions for git operations
 */

import { getPlatform } from '../platform.js'
import type { FileStatus, GitCommit, GitResult, HistoryOptions, RepoState } from './types.js'

// ============================================================================
// Command Execution
// ============================================================================

/**
 * Execute a git command
 */
export async function execGit(command: string, cwd?: string): Promise<GitResult> {
  const shell = getPlatform().shell

  try {
    const { stdout, stderr, exitCode } = await shell.exec(`git ${command}`, {
      cwd,
    })

    return {
      success: exitCode === 0,
      output: stdout.trim(),
      error: exitCode !== 0 ? stderr.trim() || stdout.trim() : undefined,
    }
  } catch (err) {
    return {
      success: false,
      output: '',
      error: err instanceof Error ? err.message : 'Unknown error',
    }
  }
}

// ============================================================================
// Repository State
// ============================================================================

/**
 * Check if we're in a git repository
 */
export async function isGitRepo(cwd?: string): Promise<boolean> {
  const result = await execGit('rev-parse --git-dir', cwd)
  return result.success
}

/**
 * Get the root directory of the repository
 */
export async function getRepoRoot(cwd?: string): Promise<string | null> {
  const result = await execGit('rev-parse --show-toplevel', cwd)
  return result.success ? result.output : null
}

/**
 * Get current branch name
 */
export async function getCurrentBranch(cwd?: string): Promise<string | null> {
  const result = await execGit('branch --show-current', cwd)
  return result.success ? result.output : null
}

/**
 * Get current HEAD SHA
 */
export async function getHeadSha(cwd?: string): Promise<string | null> {
  const result = await execGit('rev-parse HEAD', cwd)
  return result.success ? result.output : null
}

/**
 * Check if working tree has uncommitted changes
 */
export async function isDirty(cwd?: string): Promise<boolean> {
  const result = await execGit('status --porcelain', cwd)
  return result.success && result.output.length > 0
}

/**
 * Get full repository state
 */
export async function getRepoState(cwd?: string): Promise<RepoState | null> {
  const isRepo = await isGitRepo(cwd)
  if (!isRepo) {
    return null
  }

  const [root, branch, sha, dirty] = await Promise.all([
    getRepoRoot(cwd),
    getCurrentBranch(cwd),
    getHeadSha(cwd),
    isDirty(cwd),
  ])

  if (!root || !sha) {
    return null
  }

  return {
    isRepo: true,
    branch: branch || 'HEAD',
    sha,
    isDirty: dirty,
    root,
  }
}

// ============================================================================
// File Status
// ============================================================================

/**
 * Get status of files in working tree
 */
export async function getFileStatuses(cwd?: string): Promise<FileStatus[]> {
  const result = await execGit('status --porcelain', cwd)
  if (!result.success) {
    return []
  }

  const statuses: FileStatus[] = []

  for (const line of result.output.split('\n')) {
    if (!line.trim()) continue

    const code = line.slice(0, 2)
    const path = line.slice(3)

    let status: FileStatus['status']
    let originalPath: string | undefined

    // Check for renamed/copied with arrow
    if (path.includes(' -> ')) {
      const [from, to] = path.split(' -> ')
      originalPath = from
      status = code.includes('R') ? 'renamed' : 'copied'
      statuses.push({ path: to, status, originalPath })
      continue
    }

    // Map status codes
    if (code.includes('M')) {
      status = 'modified'
    } else if (code.includes('A')) {
      status = 'added'
    } else if (code.includes('D')) {
      status = 'deleted'
    } else if (code === '??') {
      status = 'untracked'
    } else if (code.includes('R')) {
      status = 'renamed'
    } else if (code.includes('C')) {
      status = 'copied'
    } else {
      status = 'modified' // Default
    }

    statuses.push({ path, status })
  }

  return statuses
}

/**
 * Check if a specific file has uncommitted changes
 */
export async function fileHasChanges(path: string, cwd?: string): Promise<boolean> {
  const result = await execGit(`status --porcelain "${path}"`, cwd)
  return result.success && result.output.length > 0
}

// ============================================================================
// History
// ============================================================================

/**
 * Get commit history
 */
export async function getHistory(options: HistoryOptions = {}, cwd?: string): Promise<GitCommit[]> {
  const limit = options.limit ?? 10
  const format = '%H|%h|%s|%an|%ae|%at'

  let command = `log -${limit} --format="${format}"`

  if (options.since) {
    command += ` ${options.since}..HEAD`
  }

  if (options.author) {
    command += ` --author="${options.author}"`
  }

  if (options.paths && options.paths.length > 0) {
    command += ` -- ${options.paths.map((p) => `"${p}"`).join(' ')}`
  }

  const result = await execGit(command, cwd)
  if (!result.success || !result.output) {
    return []
  }

  return result.output
    .split('\n')
    .filter(Boolean)
    .map((line) => {
      const [sha, shortSha, message, author, email, timestamp] = line.split('|')
      return {
        sha,
        shortSha,
        message,
        author,
        email,
        timestamp: parseInt(timestamp, 10) * 1000,
      }
    })
}

// ============================================================================
// File Operations
// ============================================================================

/**
 * Stage files for commit
 */
export async function stageFiles(paths: string[], cwd?: string): Promise<GitResult> {
  if (paths.length === 0) {
    return { success: true, output: '' }
  }

  const pathArgs = paths.map((p) => `"${p}"`).join(' ')
  return execGit(`add ${pathArgs}`, cwd)
}

/**
 * Unstage files
 */
export async function unstageFiles(paths: string[], cwd?: string): Promise<GitResult> {
  if (paths.length === 0) {
    return { success: true, output: '' }
  }

  const pathArgs = paths.map((p) => `"${p}"`).join(' ')
  return execGit(`reset HEAD ${pathArgs}`, cwd)
}

/**
 * Discard changes to files
 */
export async function discardChanges(paths: string[], cwd?: string): Promise<GitResult> {
  if (paths.length === 0) {
    return { success: true, output: '' }
  }

  const pathArgs = paths.map((p) => `"${p}"`).join(' ')
  return execGit(`checkout -- ${pathArgs}`, cwd)
}

/**
 * Create a commit
 */
export async function commit(message: string, cwd?: string): Promise<GitResult> {
  // Use heredoc-style to handle multi-line messages
  return execGit(`commit -m "${message.replace(/"/g, '\\"')}"`, cwd)
}

/**
 * Restore files from a specific commit
 */
export async function restoreFromCommit(
  sha: string,
  paths: string[],
  cwd?: string
): Promise<GitResult> {
  if (paths.length === 0) {
    return { success: true, output: '' }
  }

  const pathArgs = paths.map((p) => `"${p}"`).join(' ')
  return execGit(`checkout ${sha} -- ${pathArgs}`, cwd)
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Escape a string for use in git commands
 */
export function escapeForGit(str: string): string {
  return str.replace(/["\\]/g, '\\$&')
}

/**
 * Parse a ref (branch, tag, sha) to its full SHA
 */
export async function resolveRef(ref: string, cwd?: string): Promise<string | null> {
  const result = await execGit(`rev-parse ${ref}`, cwd)
  return result.success ? result.output : null
}
