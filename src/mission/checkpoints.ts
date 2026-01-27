/**
 * Delta9 Checkpoint Manager
 *
 * Git-based checkpoint system for mission rollback.
 * Checkpoints are stored in .delta9/checkpoints/ as JSON metadata
 * with corresponding git commits.
 */

import { existsSync, readFileSync, writeFileSync, readdirSync, rmSync } from 'node:fs'
import { join } from 'node:path'
import { execSync } from 'node:child_process'
import { nanoid } from 'nanoid'
import { getCheckpointsDir, ensureCheckpointsDir } from '../lib/paths.js'
import { appendHistory } from './history.js'
import { getNamedLogger } from '../lib/logger.js'

const log = getNamedLogger('checkpoints')

// =============================================================================
// Types
// =============================================================================

export interface Checkpoint {
  /** Unique checkpoint ID */
  id: string
  /** Human-readable name (e.g., "obj-1-complete") */
  name: string
  /** Associated mission ID */
  missionId: string
  /** Associated objective ID (if triggered by objective completion) */
  objectiveId?: string
  /** When checkpoint was created */
  createdAt: string
  /** Git commit SHA */
  gitCommit: string
  /** Files included in checkpoint */
  files: string[]
  /** Description of what this checkpoint captures */
  description?: string
  /** Whether this was auto-created */
  auto: boolean
}

export interface CheckpointOptions {
  /** Mission ID to associate */
  missionId: string
  /** Objective ID (for auto-checkpoints) */
  objectiveId?: string
  /** Description */
  description?: string
  /** Whether auto-created */
  auto?: boolean
  /** Specific files to include (default: all staged + modified) */
  files?: string[]
}

export interface RestoreResult {
  /** Whether restore was successful */
  success: boolean
  /** Checkpoint that was restored */
  checkpoint: Checkpoint
  /** Files that were restored */
  filesRestored: string[]
  /** Error message if failed */
  error?: string
}

// =============================================================================
// Checkpoint Manager
// =============================================================================

export class CheckpointManager {
  private cwd: string

  constructor(cwd: string) {
    this.cwd = cwd
  }

  // ===========================================================================
  // Git Helpers
  // ===========================================================================

  /**
   * Check if git is initialized in the project
   */
  isGitInitialized(): boolean {
    try {
      execSync('git rev-parse --is-inside-work-tree', {
        cwd: this.cwd,
        stdio: 'pipe',
      })
      return true
    } catch {
      return false
    }
  }

  /**
   * Get current git commit SHA
   */
  getCurrentCommit(): string | null {
    try {
      return execSync('git rev-parse HEAD', {
        cwd: this.cwd,
        encoding: 'utf-8',
      }).trim()
    } catch {
      return null
    }
  }

  /**
   * Get list of modified/staged files
   */
  getChangedFiles(): string[] {
    try {
      const staged = execSync('git diff --cached --name-only', {
        cwd: this.cwd,
        encoding: 'utf-8',
      }).trim()

      const modified = execSync('git diff --name-only', {
        cwd: this.cwd,
        encoding: 'utf-8',
      }).trim()

      const untracked = execSync('git ls-files --others --exclude-standard', {
        cwd: this.cwd,
        encoding: 'utf-8',
      }).trim()

      const files = new Set<string>()

      if (staged) staged.split('\n').forEach((f) => files.add(f))
      if (modified) modified.split('\n').forEach((f) => files.add(f))
      if (untracked) untracked.split('\n').forEach((f) => files.add(f))

      return Array.from(files).filter((f) => f.length > 0)
    } catch {
      return []
    }
  }

  /**
   * Create a git commit for checkpoint
   */
  private createGitCommit(name: string, files: string[]): string | null {
    try {
      // Stage all changed files
      if (files.length > 0) {
        execSync(`git add ${files.map((f) => `"${f}"`).join(' ')}`, {
          cwd: this.cwd,
          stdio: 'pipe',
        })
      } else {
        // Stage all changes if no specific files
        execSync('git add -A', {
          cwd: this.cwd,
          stdio: 'pipe',
        })
      }

      // Check if there are staged changes
      const staged = execSync('git diff --cached --name-only', {
        cwd: this.cwd,
        encoding: 'utf-8',
      }).trim()

      if (!staged) {
        // No changes to commit, return current HEAD
        return this.getCurrentCommit()
      }

      // Create commit
      const message = `[Delta9 Checkpoint] ${name}`
      execSync(`git commit -m "${message}"`, {
        cwd: this.cwd,
        stdio: 'pipe',
      })

      return this.getCurrentCommit()
    } catch (error) {
      log.error(
        `Failed to create git commit: ${error instanceof Error ? error.message : String(error)}`
      )
      return null
    }
  }

  /**
   * Restore git to a specific commit
   */
  private restoreToCommit(commit: string): boolean {
    try {
      // Hard reset to the checkpoint commit
      execSync(`git reset --hard ${commit}`, {
        cwd: this.cwd,
        stdio: 'pipe',
      })
      return true
    } catch (error) {
      log.error(
        `Failed to restore to commit: ${error instanceof Error ? error.message : String(error)}`
      )
      return false
    }
  }

  // ===========================================================================
  // Checkpoint Operations
  // ===========================================================================

  /**
   * Create a new checkpoint
   */
  create(name: string, options: CheckpointOptions): Checkpoint | null {
    if (!this.isGitInitialized()) {
      log.error('Git is not initialized in this project')
      return null
    }

    ensureCheckpointsDir(this.cwd)

    const files = options.files || this.getChangedFiles()
    const gitCommit = this.createGitCommit(name, files)

    if (!gitCommit) {
      log.error('Failed to create git commit for checkpoint')
      return null
    }

    const checkpoint: Checkpoint = {
      id: `chk_${nanoid(8)}`,
      name,
      missionId: options.missionId,
      objectiveId: options.objectiveId,
      createdAt: new Date().toISOString(),
      gitCommit,
      files,
      description: options.description,
      auto: options.auto || false,
    }

    // Save checkpoint metadata
    const checkpointPath = join(getCheckpointsDir(this.cwd), `${checkpoint.id}.json`)
    writeFileSync(checkpointPath, JSON.stringify(checkpoint, null, 2), 'utf-8')

    // Log history event
    appendHistory(this.cwd, {
      type: 'checkpoint_created',
      timestamp: checkpoint.createdAt,
      missionId: options.missionId,
      objectiveId: options.objectiveId,
      data: {
        checkpointId: checkpoint.id,
        name,
        gitCommit,
        filesCount: files.length,
        auto: checkpoint.auto,
      },
    })

    return checkpoint
  }

  /**
   * List all checkpoints for a mission
   */
  list(missionId?: string): Checkpoint[] {
    const checkpointsDir = getCheckpointsDir(this.cwd)

    if (!existsSync(checkpointsDir)) {
      return []
    }

    const files = readdirSync(checkpointsDir).filter((f) => f.endsWith('.json'))
    const checkpoints: Checkpoint[] = []

    for (const file of files) {
      try {
        const content = readFileSync(join(checkpointsDir, file), 'utf-8')
        const checkpoint = JSON.parse(content) as Checkpoint

        if (!missionId || checkpoint.missionId === missionId) {
          checkpoints.push(checkpoint)
        }
      } catch {
        // Skip invalid checkpoint files
      }
    }

    // Sort by creation time (newest first)
    return checkpoints.sort(
      (a, b) => new Date(b.createdAt).getTime() - new Date(a.createdAt).getTime()
    )
  }

  /**
   * Get a checkpoint by ID
   */
  get(id: string): Checkpoint | null {
    const checkpointPath = join(getCheckpointsDir(this.cwd), `${id}.json`)

    if (!existsSync(checkpointPath)) {
      return null
    }

    try {
      const content = readFileSync(checkpointPath, 'utf-8')
      return JSON.parse(content) as Checkpoint
    } catch {
      return null
    }
  }

  /**
   * Get a checkpoint by name
   */
  getByName(name: string): Checkpoint | null {
    const checkpoints = this.list()
    return checkpoints.find((c) => c.name === name) || null
  }

  /**
   * Restore to a checkpoint
   */
  restore(id: string): RestoreResult {
    const checkpoint = this.get(id)

    if (!checkpoint) {
      return {
        success: false,
        checkpoint: null as unknown as Checkpoint,
        filesRestored: [],
        error: `Checkpoint ${id} not found`,
      }
    }

    if (!this.isGitInitialized()) {
      return {
        success: false,
        checkpoint,
        filesRestored: [],
        error: 'Git is not initialized in this project',
      }
    }

    // First, create a backup checkpoint of current state
    const backupName = `pre-restore-${Date.now()}`
    this.createGitCommit(backupName, [])

    // Restore to checkpoint commit
    const success = this.restoreToCommit(checkpoint.gitCommit)

    if (!success) {
      return {
        success: false,
        checkpoint,
        filesRestored: [],
        error: 'Failed to restore git to checkpoint commit',
      }
    }

    // Log history event
    appendHistory(this.cwd, {
      type: 'rollback_executed',
      timestamp: new Date().toISOString(),
      missionId: checkpoint.missionId,
      data: {
        checkpointId: checkpoint.id,
        name: checkpoint.name,
        gitCommit: checkpoint.gitCommit,
        filesRestored: checkpoint.files.length,
      },
    })

    return {
      success: true,
      checkpoint,
      filesRestored: checkpoint.files,
    }
  }

  /**
   * Delete a checkpoint
   */
  delete(id: string): boolean {
    const checkpointPath = join(getCheckpointsDir(this.cwd), `${id}.json`)

    if (!existsSync(checkpointPath)) {
      return false
    }

    try {
      rmSync(checkpointPath)
      return true
    } catch {
      return false
    }
  }

  /**
   * Delete all checkpoints for a mission
   */
  deleteAll(missionId: string): number {
    const checkpoints = this.list(missionId)
    let deleted = 0

    for (const checkpoint of checkpoints) {
      if (this.delete(checkpoint.id)) {
        deleted++
      }
    }

    return deleted
  }

  /**
   * Get the latest checkpoint for a mission
   */
  getLatest(missionId: string): Checkpoint | null {
    const checkpoints = this.list(missionId)
    return checkpoints.length > 0 ? checkpoints[0] : null
  }
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Create a checkpoint manager for a project
 */
export function createCheckpointManager(cwd: string): CheckpointManager {
  return new CheckpointManager(cwd)
}

/**
 * Generate a checkpoint name for an objective
 */
export function generateObjectiveCheckpointName(objectiveId: string, index: number): string {
  return `obj-${index + 1}-${objectiveId.slice(-6)}`
}

/**
 * Describe a checkpoint in human-readable format
 */
export function describeCheckpoint(checkpoint: Checkpoint): string {
  const lines: string[] = []

  lines.push(`Checkpoint: ${checkpoint.name}`)
  lines.push(`ID: ${checkpoint.id}`)
  lines.push(`Created: ${new Date(checkpoint.createdAt).toLocaleString()}`)
  lines.push(`Git Commit: ${checkpoint.gitCommit.slice(0, 8)}`)
  lines.push(`Files: ${checkpoint.files.length}`)

  if (checkpoint.description) {
    lines.push(`Description: ${checkpoint.description}`)
  }

  if (checkpoint.auto) {
    lines.push('Type: Auto-created')
  }

  return lines.join('\n')
}
