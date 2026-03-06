import { execFile } from 'node:child_process'
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import { promisify } from 'node:util'
import { afterEach, describe, expect, it } from 'vitest'
import { ShadowSnapshotManager } from './shadow-snapshots.js'

const execFileAsync = promisify(execFile)

async function runGit(cwd: string, args: string[]): Promise<string> {
  const { stdout } = await execFileAsync('git', ['-C', cwd, ...args], {
    maxBuffer: 10 * 1024 * 1024,
  })
  return stdout.toString().trim()
}

describe('ShadowSnapshotManager', () => {
  const tempDirs: string[] = []

  afterEach(async () => {
    for (const dir of tempDirs) {
      await rm(dir, { recursive: true, force: true })
    }
    tempDirs.length = 0
  })

  async function makeDirs(): Promise<{ projectDir: string; shadowDir: string }> {
    const root = await mkdtemp(path.join(os.tmpdir(), 'ava-shadow-'))
    const projectDir = path.join(root, 'project')
    const shadowDir = path.join(root, 'shadow')
    tempDirs.push(root)
    await mkdir(projectDir, { recursive: true })
    await mkdir(shadowDir, { recursive: true })
    return { projectDir, shadowDir }
  }

  it('init creates a shadow git repo', async () => {
    const { projectDir, shadowDir } = await makeDirs()
    const manager = new ShadowSnapshotManager(projectDir, shadowDir)

    await manager.init()

    const gitDir = path.join(shadowDir, '.git')
    const insideGit = await runGit(shadowDir, ['rev-parse', '--is-inside-work-tree'])
    expect(insideGit).toBe('true')
    await expect(readFile(path.join(gitDir, 'config'), 'utf8')).resolves.toContain('[core]')
  })

  it('take creates snapshot with metadata', async () => {
    const { projectDir, shadowDir } = await makeDirs()
    await writeFile(path.join(projectDir, 'note.txt'), 'v1', 'utf8')
    const manager = new ShadowSnapshotManager(projectDir, shadowDir)
    await manager.init()

    const snapshot = await manager.take('s1', 'first snapshot')

    expect(snapshot.id.length).toBeGreaterThan(0)
    expect(snapshot.sessionId).toBe('s1')
    expect(snapshot.message).toBe('first snapshot')
    expect(snapshot.commitHash.length).toBeGreaterThan(6)

    const listed = await manager.list('s1')
    expect(listed).toHaveLength(1)
    expect(listed[0]?.id).toBe(snapshot.id)
  })

  it('restore recovers previous file state', async () => {
    const { projectDir, shadowDir } = await makeDirs()
    const file = path.join(projectDir, 'state.txt')
    await writeFile(file, 'one', 'utf8')
    const manager = new ShadowSnapshotManager(projectDir, shadowDir)
    await manager.init()

    const first = await manager.take('s1', 'state one')
    await writeFile(file, 'two', 'utf8')
    await manager.take('s1', 'state two')

    await manager.restore(first.id)

    await expect(readFile(file, 'utf8')).resolves.toBe('one')
  })

  it('prune removes old snapshots', async () => {
    const { projectDir, shadowDir } = await makeDirs()
    const file = path.join(projectDir, 'prune.txt')
    const manager = new ShadowSnapshotManager(projectDir, shadowDir)
    await manager.init()

    await writeFile(file, 'a', 'utf8')
    await manager.take('s1', 'a')
    await writeFile(file, 'b', 'utf8')
    await manager.take('s1', 'b')
    await writeFile(file, 'c', 'utf8')
    await manager.take('s1', 'c')

    const removed = await manager.prune(2)
    const listed = await manager.list('s1')

    expect(removed).toBe(1)
    expect(listed).toHaveLength(2)
  })

  it('does not affect project git status when taking snapshots', async () => {
    const { projectDir, shadowDir } = await makeDirs()
    const file = path.join(projectDir, 'tracked.txt')
    await writeFile(file, 'clean', 'utf8')

    await runGit(projectDir, ['init'])
    await runGit(projectDir, ['config', 'user.email', 'ava-test@local'])
    await runGit(projectDir, ['config', 'user.name', 'AVA Test'])
    await runGit(projectDir, ['add', '.'])
    await runGit(projectDir, ['commit', '-m', 'init'])

    const manager = new ShadowSnapshotManager(projectDir, shadowDir)
    await manager.init()
    await manager.take('s1', 'shadow snapshot')

    const status = await runGit(projectDir, ['status', '--short'])
    expect(status).toBe('')
  })
})
