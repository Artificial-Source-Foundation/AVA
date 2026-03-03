/**
 * Git snapshot manager backed by a separate .ava/snapshots repository.
 */

import type { IShell } from '@ava/core-v2/platform'
import {
  createSnapshot,
  listSnapshots,
  restoreSnapshot as restoreSnapshotCommit,
} from './snapshot-repo.js'
import type { GitConfig, GitSnapshot } from './types.js'
import { DEFAULT_GIT_CONFIG } from './types.js'

export interface SnapshotManager {
  createSnapshot(cwd: string, message: string, files: string[]): Promise<GitSnapshot | null>
  restoreSnapshot(cwd: string, hash: string): Promise<boolean>
  getLatestSnapshot(): GitSnapshot | null
  getSnapshots(): GitSnapshot[]
  clear(): void
}

export function createSnapshotManager(
  shell: IShell,
  config: GitConfig = DEFAULT_GIT_CONFIG
): SnapshotManager {
  const snapshots: GitSnapshot[] = []

  return {
    async createSnapshot(
      cwd: string,
      message: string,
      files: string[]
    ): Promise<GitSnapshot | null> {
      try {
        const commit = await createSnapshot(shell, cwd, message)
        if (!commit) {
          return null
        }

        const snapshot: GitSnapshot = {
          hash: commit.id,
          message,
          timestamp: commit.timestamp,
          files,
        }

        snapshots.push(snapshot)

        // Cap at maxSnapshots
        while (snapshots.length > config.maxSnapshots) {
          snapshots.shift()
        }

        return snapshot
      } catch {
        return null
      }
    },

    async restoreSnapshot(cwd: string, hash: string): Promise<boolean> {
      try {
        return await restoreSnapshotCommit(shell, cwd, hash)
      } catch {
        return false
      }
    },

    getLatestSnapshot(): GitSnapshot | null {
      return snapshots.length > 0 ? snapshots[snapshots.length - 1]! : null
    },

    getSnapshots(): GitSnapshot[] {
      return [...snapshots]
    },

    clear(): void {
      snapshots.length = 0
    },
  }
}

/**
 * Check if the current directory is a git repository.
 */
export async function isGitRepo(shell: IShell, cwd: string): Promise<boolean> {
  try {
    const result = await shell.exec(`git -C "${cwd}" rev-parse --is-inside-work-tree`)
    return result.stdout.trim() === 'true'
  } catch {
    return false
  }
}

export async function getSnapshotCommits(shell: IShell, cwd: string): Promise<GitSnapshot[]> {
  const commits = await listSnapshots(shell, cwd)
  return commits.map((commit) => ({
    hash: commit.id,
    message: commit.message,
    timestamp: commit.timestamp,
    files: [],
  }))
}
