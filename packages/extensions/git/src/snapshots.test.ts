import { MockShell } from '@ava/core-v2/__test-utils__/mock-platform'
import { describe, expect, it } from 'vitest'
import { createSnapshotManager, isGitRepo } from './snapshots.js'

function setSnapshotInit(shell: MockShell, cwd: string): void {
  shell.setResult(`mkdir -p "${cwd}/.ava/snapshots" "${cwd}/.ava/snapshots-worktree"`, {
    stdout: '',
    stderr: '',
    exitCode: 0,
  })
  shell.setResult(`git --git-dir="${cwd}/.ava/snapshots" rev-parse --git-dir`, {
    stdout: '',
    stderr: 'fatal: not a git repository',
    exitCode: 128,
  })
  shell.setResult(`git init --bare "${cwd}/.ava/snapshots"`, {
    stdout: 'Initialized empty Git repository',
    stderr: '',
    exitCode: 0,
  })
}

describe('createSnapshotManager', () => {
  it('creates a snapshot in .ava/snapshots repo', async () => {
    const shell = new MockShell()
    setSnapshotInit(shell, '/project')

    shell.setResult(
      'rm -rf "/project/.ava/snapshots-worktree"/* && git -C "/project" archive --format=tar HEAD | tar -x -C "/project/.ava/snapshots-worktree"',
      {
        stdout: '',
        stderr: '',
        exitCode: 0,
      }
    )
    shell.setResult(
      'git --git-dir="/project/.ava/snapshots" --work-tree="/project/.ava/snapshots-worktree" add -A',
      {
        stdout: '',
        stderr: '',
        exitCode: 0,
      }
    )
    shell.setResult(
      'git --git-dir="/project/.ava/snapshots" --work-tree="/project/.ava/snapshots-worktree" -c user.name="AVA" -c user.email="ava@local" commit --allow-empty -m "test snapshot"',
      {
        stdout: '[main] test snapshot',
        stderr: '',
        exitCode: 0,
      }
    )
    shell.setResult('git --git-dir="/project/.ava/snapshots" rev-parse HEAD', {
      stdout: 'abc123def456\n',
      stderr: '',
      exitCode: 0,
    })

    const manager = createSnapshotManager(shell)
    const snapshot = await manager.createSnapshot('/project', 'test snapshot', ['file.ts'])

    expect(snapshot).not.toBeNull()
    expect(snapshot!.hash).toBe('abc123def456')
    expect(snapshot!.files).toEqual(['file.ts'])
  })

  it('returns null when archive export fails', async () => {
    const shell = new MockShell()
    setSnapshotInit(shell, '/project')
    shell.setResult(
      'rm -rf "/project/.ava/snapshots-worktree"/* && git -C "/project" archive --format=tar HEAD | tar -x -C "/project/.ava/snapshots-worktree"',
      {
        stdout: '',
        stderr: 'archive failed',
        exitCode: 1,
      }
    )
    shell.setResult('git --git-dir="/project/.ava/snapshots" rev-parse HEAD', {
      stdout: '',
      stderr: '',
      exitCode: 0,
    })

    const manager = createSnapshotManager(shell)
    const snapshot = await manager.createSnapshot('/project', 'empty', [])
    expect(snapshot).toBeNull()
  })

  it('caps snapshots at maxSnapshots', async () => {
    const shell = new MockShell()
    const manager = createSnapshotManager(shell, {
      autoCommit: false,
      snapshotOnToolCall: true,
      maxSnapshots: 2,
    })

    for (let i = 0; i < 3; i++) {
      setSnapshotInit(shell, '/project')
      shell.setResult(
        'rm -rf "/project/.ava/snapshots-worktree"/* && git -C "/project" archive --format=tar HEAD | tar -x -C "/project/.ava/snapshots-worktree"',
        {
          stdout: '',
          stderr: '',
          exitCode: 0,
        }
      )
      shell.setResult(
        'git --git-dir="/project/.ava/snapshots" --work-tree="/project/.ava/snapshots-worktree" add -A',
        {
          stdout: '',
          stderr: '',
          exitCode: 0,
        }
      )
      shell.setResult(
        `git --git-dir="/project/.ava/snapshots" --work-tree="/project/.ava/snapshots-worktree" -c user.name="AVA" -c user.email="ava@local" commit --allow-empty -m "snap-${i}"`,
        {
          stdout: `[main] snap-${i}`,
          stderr: '',
          exitCode: 0,
        }
      )
      shell.setResult('git --git-dir="/project/.ava/snapshots" rev-parse HEAD', {
        stdout: `hash-${i}\n`,
        stderr: '',
        exitCode: 0,
      })
      await manager.createSnapshot('/project', `snap-${i}`, [])
    }

    expect(manager.getSnapshots()).toHaveLength(2)
  })

  it('clears all snapshots', async () => {
    const shell = new MockShell()
    setSnapshotInit(shell, '/project')
    shell.setResult(
      'rm -rf "/project/.ava/snapshots-worktree"/* && git -C "/project" archive --format=tar HEAD | tar -x -C "/project/.ava/snapshots-worktree"',
      {
        stdout: '',
        stderr: '',
        exitCode: 0,
      }
    )
    shell.setResult(
      'git --git-dir="/project/.ava/snapshots" --work-tree="/project/.ava/snapshots-worktree" add -A',
      {
        stdout: '',
        stderr: '',
        exitCode: 0,
      }
    )
    shell.setResult(
      'git --git-dir="/project/.ava/snapshots" --work-tree="/project/.ava/snapshots-worktree" -c user.name="AVA" -c user.email="ava@local" commit --allow-empty -m "snap"',
      {
        stdout: '[main] snap',
        stderr: '',
        exitCode: 0,
      }
    )
    shell.setResult('git --git-dir="/project/.ava/snapshots" rev-parse HEAD', {
      stdout: 'hash\n',
      stderr: '',
      exitCode: 0,
    })

    const manager = createSnapshotManager(shell)
    await manager.createSnapshot('/project', 'snap', [])
    expect(manager.getSnapshots()).toHaveLength(1)
    manager.clear()
    expect(manager.getSnapshots()).toHaveLength(0)
  })
})

describe('isGitRepo', () => {
  it('returns true for git repos', async () => {
    const shell = new MockShell()
    shell.setResult('git -C "/project" rev-parse --is-inside-work-tree', {
      stdout: 'true\n',
      stderr: '',
      exitCode: 0,
    })
    expect(await isGitRepo(shell, '/project')).toBe(true)
  })

  it('returns false for non-git dirs', async () => {
    const shell = new MockShell()
    shell.setResult('git -C "/tmp" rev-parse --is-inside-work-tree', {
      stdout: '',
      stderr: 'fatal: not a git repository',
      exitCode: 128,
    })
    expect(await isGitRepo(shell, '/tmp')).toBe(false)
  })
})
