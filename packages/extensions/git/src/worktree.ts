/**
 * Git worktree isolation — creates per-session worktrees for safe delegation.
 */

import { getPlatform } from '@ava/core-v2/platform'

export interface WorktreeResult {
  path: string
  branch: string
}

/**
 * Create a git worktree with a session-specific branch.
 * The worktree is placed at `<cwd>/.ava-worktrees/ava-session-<id>`.
 */
export async function createWorktree(cwd: string, sessionId: string): Promise<WorktreeResult> {
  const shell = getPlatform().shell
  const branch = `ava-session-${sessionId.slice(0, 8)}`
  const worktreePath = `${cwd}/.ava-worktrees/${branch}`
  await shell.exec(`cd "${cwd}" && git worktree add -b "${branch}" "${worktreePath}"`)
  return { path: worktreePath, branch }
}

/**
 * Remove a git worktree and its associated branch.
 */
export async function removeWorktree(cwd: string, worktreePath: string): Promise<void> {
  const shell = getPlatform().shell
  await shell.exec(`cd "${cwd}" && git worktree remove --force "${worktreePath}"`)
}
