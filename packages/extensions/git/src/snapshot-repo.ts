import type { IShell } from '@ava/core-v2/platform'

export interface SnapshotCommit {
  id: string
  timestamp: number
  message: string
}

function quote(value: string): string {
  return value.replace(/"/g, '\\"')
}

function repoPath(projectDir: string): string {
  return `${projectDir}/.ava/snapshots`
}

function worktreePath(projectDir: string): string {
  return `${projectDir}/.ava/snapshots-worktree`
}

async function ensureRepo(shell: IShell, projectDir: string): Promise<void> {
  const repo = repoPath(projectDir)
  const worktree = worktreePath(projectDir)
  await shell.exec(`mkdir -p "${quote(repo)}" "${quote(worktree)}"`)

  const existing = await shell.exec(`git --git-dir="${quote(repo)}" rev-parse --git-dir`)
  if (existing.exitCode !== 0) {
    await shell.exec(`git init --bare "${quote(repo)}"`)
  }
}

export async function createSnapshot(
  shell: IShell,
  projectDir: string,
  message: string
): Promise<SnapshotCommit | null> {
  const repo = repoPath(projectDir)
  const worktree = worktreePath(projectDir)
  await ensureRepo(shell, projectDir)

  const archive = await shell.exec(
    `rm -rf "${quote(worktree)}"/* && git -C "${quote(projectDir)}" archive --format=tar HEAD | tar -x -C "${quote(worktree)}"`
  )
  if (archive.exitCode !== 0) {
    return null
  }

  await shell.exec(`git --git-dir="${quote(repo)}" --work-tree="${quote(worktree)}" add -A`)
  const commit = await shell.exec(
    `git --git-dir="${quote(repo)}" --work-tree="${quote(worktree)}" -c user.name="AVA" -c user.email="ava@local" commit --allow-empty -m "${quote(message)}"`
  )
  if (commit.exitCode !== 0) {
    return null
  }

  const hash = await shell.exec(`git --git-dir="${quote(repo)}" rev-parse HEAD`)
  const id = hash.stdout.trim()
  if (!id) {
    return null
  }

  return {
    id,
    timestamp: Date.now(),
    message,
  }
}

export async function listSnapshots(shell: IShell, projectDir: string): Promise<SnapshotCommit[]> {
  const repo = repoPath(projectDir)
  await ensureRepo(shell, projectDir)

  const result = await shell.exec(
    `git --git-dir="${quote(repo)}" log --pretty=format:"%H|%ct|%s" --no-decorate`
  )
  if (result.exitCode !== 0 || result.stdout.trim() === '') {
    return []
  }

  return result.stdout
    .split('\n')
    .map((line) => line.trim())
    .filter(Boolean)
    .map((line) => {
      const [id = '', epoch = '0', message = ''] = line.split('|')
      return {
        id,
        timestamp: Number(epoch) * 1000,
        message,
      }
    })
}

export async function restoreSnapshot(
  shell: IShell,
  projectDir: string,
  snapshotId: string
): Promise<boolean> {
  const repo = repoPath(projectDir)
  await ensureRepo(shell, projectDir)

  const result = await shell.exec(
    `git --git-dir="${quote(repo)}" --work-tree="${quote(projectDir)}" checkout "${quote(snapshotId)}" -- .`
  )

  return result.exitCode === 0
}
