/**
 * Delta9 Git Sync
 *
 * Git integration for epics and tasks:
 * - Branch creation for epics
 * - Commit tracking for task completion
 * - Objective checkpoints as tags
 * - File change detection
 *
 * @example
 * ```typescript
 * import { GitSync } from './mission/git-sync'
 *
 * const git = new GitSync({ cwd: process.cwd() })
 *
 * // Create branch for epic
 * const branch = await git.createEpicBranch(epic)
 *
 * // Get changed files for a task
 * const files = await git.getTaskFileChanges(task)
 *
 * // Create checkpoint tag
 * await git.checkpointObjective(objective)
 * ```
 */

import { spawn } from 'node:child_process'
import type { Epic } from './epic.js'

// =============================================================================
// Types
// =============================================================================

export interface GitSyncConfig {
  /** Working directory */
  cwd: string
  /** Whether to actually run git commands (false for testing) */
  dryRun?: boolean
}

export interface GitResult {
  success: boolean
  stdout?: string
  stderr?: string
  error?: string
}

export interface BranchInfo {
  name: string
  isNew: boolean
  baseBranch: string
}

// =============================================================================
// Git Sync Class
// =============================================================================

export class GitSync {
  private cwd: string
  private dryRun: boolean

  constructor(config: GitSyncConfig) {
    this.cwd = config.cwd
    this.dryRun = config.dryRun ?? false
  }

  // ===========================================================================
  // Branch Management
  // ===========================================================================

  /**
   * Create a branch for an epic
   */
  async createEpicBranch(
    epic: Epic,
    baseBranch?: string
  ): Promise<{ success: boolean; branch?: BranchInfo; error?: string }> {
    const branchName = this.generateBranchName(epic)
    const base = baseBranch ?? (await this.getDefaultBranch())

    if (this.dryRun) {
      return {
        success: true,
        branch: {
          name: branchName,
          isNew: true,
          baseBranch: base,
        },
      }
    }

    // Check if branch already exists
    const existsResult = await this.run(['branch', '--list', branchName])
    if (existsResult.success && existsResult.stdout?.trim()) {
      // Branch exists, just return it
      return {
        success: true,
        branch: {
          name: branchName,
          isNew: false,
          baseBranch: base,
        },
      }
    }

    // Create new branch
    const createResult = await this.run(['checkout', '-b', branchName, base])
    if (!createResult.success) {
      return {
        success: false,
        error: createResult.error || createResult.stderr || 'Failed to create branch',
      }
    }

    return {
      success: true,
      branch: {
        name: branchName,
        isNew: true,
        baseBranch: base,
      },
    }
  }

  /**
   * Switch to a branch
   */
  async switchBranch(branchName: string): Promise<GitResult> {
    if (this.dryRun) {
      return { success: true, stdout: `Would switch to ${branchName}` }
    }
    return this.run(['checkout', branchName])
  }

  /**
   * Get current branch
   */
  async getCurrentBranch(): Promise<string> {
    if (this.dryRun) {
      return 'main'
    }
    const result = await this.run(['rev-parse', '--abbrev-ref', 'HEAD'])
    return result.success ? (result.stdout?.trim() ?? 'main') : 'main'
  }

  /**
   * Get default branch (main or master)
   */
  async getDefaultBranch(): Promise<string> {
    if (this.dryRun) {
      return 'main'
    }

    // Try to get from remote
    const remoteResult = await this.run(['remote', 'show', 'origin'])
    if (remoteResult.success && remoteResult.stdout) {
      const match = remoteResult.stdout.match(/HEAD branch:\s*(\S+)/)
      if (match) {
        return match[1]
      }
    }

    // Check if main exists
    const mainResult = await this.run(['branch', '--list', 'main'])
    if (mainResult.success && mainResult.stdout?.trim()) {
      return 'main'
    }

    return 'master'
  }

  // ===========================================================================
  // Commit Operations
  // ===========================================================================

  /**
   * Create a commit for task completion
   */
  async commitTask(taskId: string, taskDescription: string, files?: string[]): Promise<GitResult> {
    const message = `task(${taskId}): ${taskDescription}`

    if (this.dryRun) {
      return { success: true, stdout: `Would commit: ${message}` }
    }

    // Stage files
    if (files && files.length > 0) {
      const addResult = await this.run(['add', ...files])
      if (!addResult.success) {
        return addResult
      }
    } else {
      // Stage all changes
      const addResult = await this.run(['add', '-A'])
      if (!addResult.success) {
        return addResult
      }
    }

    // Check if there are staged changes
    const statusResult = await this.run(['diff', '--cached', '--quiet'])
    if (statusResult.success) {
      // No changes to commit
      return { success: true, stdout: 'No changes to commit' }
    }

    // Create commit
    return this.run(['commit', '-m', message])
  }

  /**
   * Get files changed in working directory
   */
  async getChangedFiles(): Promise<string[]> {
    if (this.dryRun) {
      return []
    }

    const result = await this.run(['status', '--porcelain'])
    if (!result.success || !result.stdout) {
      return []
    }

    return result.stdout
      .split('\n')
      .filter((line) => line.trim())
      .map((line) => line.substring(3).trim())
      .filter((file) => file.length > 0)
  }

  /**
   * Get files changed since a commit
   */
  async getFilesSinceCommit(commitHash: string): Promise<string[]> {
    if (this.dryRun) {
      return []
    }

    const result = await this.run(['diff', '--name-only', commitHash, 'HEAD'])
    if (!result.success || !result.stdout) {
      return []
    }

    return result.stdout.split('\n').filter((line) => line.trim())
  }

  // ===========================================================================
  // Checkpoint (Tags)
  // ===========================================================================

  /**
   * Create a checkpoint tag for an objective
   */
  async checkpointObjective(
    objectiveId: string,
    objectiveDescription: string,
    epicId?: string
  ): Promise<{ success: boolean; tag?: string; error?: string }> {
    const tagName = this.generateTagName(objectiveId, epicId)
    const message = `Checkpoint: ${objectiveDescription}`

    if (this.dryRun) {
      return { success: true, tag: tagName }
    }

    const result = await this.run(['tag', '-a', tagName, '-m', message])
    if (!result.success) {
      return {
        success: false,
        error: result.error || result.stderr || 'Failed to create tag',
      }
    }

    return { success: true, tag: tagName }
  }

  /**
   * List checkpoint tags
   */
  async listCheckpoints(epicId?: string): Promise<string[]> {
    if (this.dryRun) {
      return []
    }

    const pattern = epicId ? `checkpoint/${epicId}/*` : 'checkpoint/*'
    const result = await this.run(['tag', '-l', pattern])

    if (!result.success || !result.stdout) {
      return []
    }

    return result.stdout.split('\n').filter((line) => line.trim())
  }

  // ===========================================================================
  // Status Checks
  // ===========================================================================

  /**
   * Check if directory is a git repository
   */
  async isGitRepo(): Promise<boolean> {
    if (this.dryRun) {
      return true
    }
    const result = await this.run(['rev-parse', '--is-inside-work-tree'])
    return result.success && result.stdout?.trim() === 'true'
  }

  /**
   * Check if working directory is clean
   */
  async isClean(): Promise<boolean> {
    if (this.dryRun) {
      return true
    }
    const result = await this.run(['status', '--porcelain'])
    return result.success && !result.stdout?.trim()
  }

  /**
   * Get current commit hash
   */
  async getCurrentCommit(): Promise<string | undefined> {
    if (this.dryRun) {
      return 'abc123'
    }
    const result = await this.run(['rev-parse', 'HEAD'])
    return result.success ? result.stdout?.trim() : undefined
  }

  // ===========================================================================
  // Private Helpers
  // ===========================================================================

  /**
   * Generate branch name from epic
   */
  private generateBranchName(epic: Epic): string {
    const slug = epic.title
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, '-')
      .replace(/(^-|-$)/g, '')
      .substring(0, 40)
    return `epic/${epic.id.replace('epic-', '')}/${slug}`
  }

  /**
   * Generate tag name for checkpoint
   */
  private generateTagName(objectiveId: string, epicId?: string): string {
    const timestamp = new Date().toISOString().replace(/[:.]/g, '-').substring(0, 19)
    if (epicId) {
      return `checkpoint/${epicId}/${objectiveId}-${timestamp}`
    }
    return `checkpoint/${objectiveId}-${timestamp}`
  }

  /**
   * Run a git command
   */
  private run(args: string[]): Promise<GitResult> {
    return new Promise((resolve) => {
      const proc = spawn('git', args, { cwd: this.cwd })
      let stdout = ''
      let stderr = ''

      proc.stdout.on('data', (data) => {
        stdout += data.toString()
      })

      proc.stderr.on('data', (data) => {
        stderr += data.toString()
      })

      proc.on('error', (error) => {
        resolve({
          success: false,
          error: error.message,
        })
      })

      proc.on('close', (code) => {
        resolve({
          success: code === 0,
          stdout,
          stderr,
        })
      })
    })
  }
}
