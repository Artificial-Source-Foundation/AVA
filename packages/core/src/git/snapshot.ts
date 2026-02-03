/**
 * Git Snapshots
 * Create and restore snapshots for safe rollback
 */

import {
  type CreateSnapshotOptions,
  DEFAULT_GIT_CONFIG,
  type GitConfig,
  type RollbackResult,
  type Snapshot,
} from './types.js'
import {
  commit,
  execGit,
  getCurrentBranch,
  getHeadSha,
  isGitRepo,
  restoreFromCommit,
  stageFiles,
} from './utils.js'

// ============================================================================
// Snapshot Storage
// ============================================================================

/** In-memory snapshot storage (could be persisted to disk) */
const snapshots = new Map<string, Snapshot>()

// ============================================================================
// Create Snapshots
// ============================================================================

/**
 * Create a snapshot before modifying files
 *
 * @param paths - Files that will be modified
 * @param config - Git configuration
 * @param options - Snapshot options
 * @returns The created snapshot, or null if git is disabled/not available
 */
export async function createSnapshot(
  paths: string[],
  config: GitConfig,
  options?: CreateSnapshotOptions
): Promise<Snapshot | null> {
  // Check if enabled
  if (!config.enabled) {
    return null
  }

  // Check if in git repo
  if (!(await isGitRepo())) {
    return null
  }

  // Get current state
  const [branch, sha] = await Promise.all([getCurrentBranch(), getHeadSha()])

  if (!sha) {
    return null
  }

  // Create snapshot record
  const snapshot: Snapshot = {
    id: generateSnapshotId(),
    sha,
    branch: branch || 'HEAD',
    message: options?.message || `${config.messagePrefix} Snapshot before changes`,
    paths,
    createdAt: Date.now(),
    description: options?.description,
  }

  // Optionally create a commit
  if (options?.commit ?? config.autoCommit) {
    if (paths.length > 0) {
      // Stage the files
      const stageResult = await stageFiles(paths)
      if (!stageResult.success) {
        // Files might not exist yet or have no changes, that's ok
        console.warn('Could not stage files:', stageResult.error)
      }

      // Create commit
      const commitResult = await commit(snapshot.message)
      if (commitResult.success) {
        // Update SHA to the new commit
        const newSha = await getHeadSha()
        if (newSha) {
          snapshot.sha = newSha
        }
      }
    }
  }

  // Store snapshot
  snapshots.set(snapshot.id, snapshot)

  return snapshot
}

/**
 * Create a snapshot of the current state without committing
 * Just records the current HEAD for potential rollback
 */
export async function createQuickSnapshot(
  paths: string[],
  description?: string
): Promise<Snapshot | null> {
  return createSnapshot(
    paths,
    { ...DEFAULT_GIT_CONFIG, autoCommit: false },
    {
      description,
      commit: false,
    }
  )
}

// ============================================================================
// Rollback
// ============================================================================

/**
 * Rollback to a previous snapshot
 *
 * @param snapshot - The snapshot to restore
 * @returns Result of the rollback operation
 */
export async function rollback(snapshot: Snapshot): Promise<RollbackResult> {
  const restoredFiles: string[] = []
  const failedFiles: string[] = []

  if (snapshot.paths.length === 0) {
    return {
      success: true,
      output: 'No files to restore',
      restoredFiles,
      failedFiles,
    }
  }

  // Restore each file from the snapshot commit
  const result = await restoreFromCommit(snapshot.sha, snapshot.paths)

  if (result.success) {
    restoredFiles.push(...snapshot.paths)
  } else {
    // Try files one by one to see which fail
    for (const path of snapshot.paths) {
      const fileResult = await restoreFromCommit(snapshot.sha, [path])
      if (fileResult.success) {
        restoredFiles.push(path)
      } else {
        failedFiles.push(path)
      }
    }
  }

  return {
    success: failedFiles.length === 0,
    output:
      failedFiles.length === 0
        ? `Restored ${restoredFiles.length} files`
        : `Restored ${restoredFiles.length} files, ${failedFiles.length} failed`,
    error: failedFiles.length > 0 ? `Failed to restore: ${failedFiles.join(', ')}` : undefined,
    restoredFiles,
    failedFiles,
  }
}

/**
 * Rollback to a snapshot by ID
 */
export async function rollbackToId(id: string): Promise<RollbackResult | null> {
  const snapshot = snapshots.get(id)
  if (!snapshot) {
    return null
  }
  return rollback(snapshot)
}

// ============================================================================
// Snapshot Management
// ============================================================================

/**
 * Get a snapshot by ID
 */
export function getSnapshot(id: string): Snapshot | undefined {
  return snapshots.get(id)
}

/**
 * Get all snapshots
 */
export function getAllSnapshots(): Snapshot[] {
  return [...snapshots.values()].sort((a, b) => b.createdAt - a.createdAt)
}

/**
 * Get recent snapshots
 */
export function getRecentSnapshots(limit = 10): Snapshot[] {
  return getAllSnapshots().slice(0, limit)
}

/**
 * Delete a snapshot
 */
export function deleteSnapshot(id: string): boolean {
  return snapshots.delete(id)
}

/**
 * Clear all snapshots
 */
export function clearSnapshots(): void {
  snapshots.clear()
}

/**
 * Get snapshots for a specific file path
 */
export function getSnapshotsForPath(path: string): Snapshot[] {
  return getAllSnapshots().filter((s) => s.paths.includes(path))
}

// ============================================================================
// Git History Integration
// ============================================================================

/**
 * Get commit history as snapshots
 * Converts git commits to snapshot format for unified interface
 */
export async function getHistoryAsSnapshots(limit = 10): Promise<Snapshot[]> {
  const result = await execGit(`log -${limit} --format="%H|%s|%aI" --name-only`)

  if (!result.success || !result.output) {
    return []
  }

  const snapshots: Snapshot[] = []
  const lines = result.output.split('\n')

  let current: Partial<Snapshot> | null = null
  const currentPaths: string[] = []

  for (const line of lines) {
    if (line.includes('|')) {
      // New commit line
      if (current?.sha) {
        snapshots.push({
          ...current,
          paths: [...currentPaths],
        } as Snapshot)
        currentPaths.length = 0
      }

      const [sha, message, date] = line.split('|')
      current = {
        id: sha.slice(0, 8),
        sha,
        branch: '', // Not available from this format
        message,
        createdAt: new Date(date).getTime(),
      }
    } else if (line.trim() && current) {
      // File path line
      currentPaths.push(line.trim())
    }
  }

  // Don't forget the last one
  if (current?.sha) {
    snapshots.push({
      ...current,
      paths: [...currentPaths],
    } as Snapshot)
  }

  return snapshots
}

// ============================================================================
// Utilities
// ============================================================================

function generateSnapshotId(): string {
  return `snap-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`
}

/**
 * Check if a snapshot is still valid (the commit still exists)
 */
export async function isSnapshotValid(snapshot: Snapshot): Promise<boolean> {
  const result = await execGit(`cat-file -t ${snapshot.sha}`)
  return result.success && result.output === 'commit'
}
