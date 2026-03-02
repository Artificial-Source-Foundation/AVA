/**
 * Git snapshot manager — creates and tracks git stash snapshots.
 */

import type { IShell } from '@ava/core-v2/platform'
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
        const result = await shell.exec(`cd "${cwd}" && git stash create "${message}"`)
        const hash = result.stdout.trim()
        if (!hash) return null

        const snapshot: GitSnapshot = {
          hash,
          message,
          timestamp: Date.now(),
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
        // Apply the stash snapshot to restore files
        await shell.exec(`cd "${cwd}" && git stash apply "${hash}"`)
        return true
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
    const result = await shell.exec(`cd "${cwd}" && git rev-parse --is-inside-work-tree`)
    return result.stdout.trim() === 'true'
  } catch {
    return false
  }
}
