import { MockShell } from '@ava/core-v2/__test-utils__/mock-platform'
import { describe, expect, it } from 'vitest'
import { createSnapshotManager, isGitRepo } from './snapshots.js'

describe('createSnapshotManager', () => {
  it('creates a snapshot when git returns a hash', async () => {
    const shell = new MockShell()
    shell.setResult('cd "/project" && git stash create "test snapshot"', {
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

  it('returns null when git returns empty hash', async () => {
    const shell = new MockShell()
    shell.setResult('cd "/project" && git stash create "empty"', {
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
      shell.setResult(`cd "/project" && git stash create "snap-${i}"`, {
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
    shell.setResult('cd "/project" && git stash create "snap"', {
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
    shell.setResult('cd "/project" && git rev-parse --is-inside-work-tree', {
      stdout: 'true\n',
      stderr: '',
      exitCode: 0,
    })
    expect(await isGitRepo(shell, '/project')).toBe(true)
  })

  it('returns false for non-git dirs', async () => {
    const shell = new MockShell()
    shell.setResult('cd "/tmp" && git rev-parse --is-inside-work-tree', {
      stdout: '',
      stderr: 'fatal: not a git repository',
      exitCode: 128,
    })
    expect(await isGitRepo(shell, '/tmp')).toBe(false)
  })
})
