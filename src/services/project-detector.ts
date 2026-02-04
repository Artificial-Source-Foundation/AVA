/**
 * Project Detector Service
 * Detects project root from a directory path using git
 */

import { Command } from '@tauri-apps/plugin-shell'
import type { DetectedProject } from '../types'

/**
 * Detect project from a directory path
 * Walks up to find git root, similar to OpenCode pattern
 */
export async function detectProject(directory: string): Promise<DetectedProject> {
  // Try to detect git repository
  try {
    const gitRoot = await runGitCommand(['rev-parse', '--show-toplevel'], directory)

    if (gitRoot) {
      // This is a git repository
      const [branch, rootCommit] = await Promise.all([
        runGitCommand(['branch', '--show-current'], gitRoot),
        runGitCommand(['rev-list', '--max-parents=0', 'HEAD'], gitRoot).then((commits) =>
          commits?.split('\n')[0]?.trim()
        ),
      ])

      const suggestedName = gitRoot.split('/').pop() || 'Project'

      return {
        rootDirectory: gitRoot,
        cwd: directory,
        isGitRepo: true,
        branch: branch || undefined,
        rootCommit: rootCommit || undefined,
        suggestedName,
      }
    }
  } catch {
    // Not a git repo or git not available
  }

  // Not a git repository - use directory as-is
  const suggestedName = directory.split('/').pop() || 'Project'

  return {
    rootDirectory: directory,
    cwd: directory,
    isGitRepo: false,
    suggestedName,
  }
}

/**
 * Get current git branch for a project
 */
export async function getCurrentBranch(directory: string): Promise<string | null> {
  try {
    const branch = await runGitCommand(['branch', '--show-current'], directory)
    return branch || null
  } catch {
    return null
  }
}

/**
 * Check if a directory is a git repository
 */
export async function isGitRepository(directory: string): Promise<boolean> {
  try {
    const result = await runGitCommand(['rev-parse', '--git-dir'], directory)
    return result !== null
  } catch {
    return false
  }
}

/**
 * Get remote origin URL
 */
export async function getRemoteUrl(directory: string): Promise<string | null> {
  try {
    const url = await runGitCommand(['remote', 'get-url', 'origin'], directory)
    return url || null
  } catch {
    return null
  }
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Run a git command and return trimmed output
 */
async function runGitCommand(args: string[], cwd: string): Promise<string | null> {
  try {
    const command = Command.create('git', args, { cwd })
    const output = await command.execute()

    if (output.code === 0) {
      return output.stdout.trim()
    }
  } catch {
    // Command failed
  }
  return null
}
