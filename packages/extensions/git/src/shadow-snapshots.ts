import { execFile } from 'node:child_process'
import { createHash, randomUUID } from 'node:crypto'
import { access, copyFile, mkdir, readdir, readFile, rm, stat, writeFile } from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import { promisify } from 'node:util'
import { dispatchCompute } from '@ava/core-v2'

export interface Snapshot {
  id: string
  sessionId: string
  timestamp: number
  message: string
  commitHash: string
}

const MANIFEST_NAME = '.shadow-snapshots.json'
const execFileAsync = promisify(execFile)

async function exists(filePath: string): Promise<boolean> {
  try {
    await access(filePath)
    return true
  } catch {
    return false
  }
}

async function clearDirectory(targetDir: string, preserve: Set<string>): Promise<void> {
  if (!(await exists(targetDir))) return

  const entries = await readdir(targetDir)
  for (const entry of entries) {
    if (preserve.has(entry)) continue
    await rm(path.join(targetDir, entry), { recursive: true, force: true })
  }
}

async function copyTree(fromDir: string, toDir: string, exclude: Set<string>): Promise<void> {
  if (!(await exists(fromDir))) return

  const entries = await readdir(fromDir)
  for (const entry of entries) {
    if (exclude.has(entry)) continue

    const fromPath = path.join(fromDir, entry)
    const toPath = path.join(toDir, entry)
    const entryStat = await stat(fromPath)

    if (entryStat.isDirectory()) {
      await mkdir(toPath, { recursive: true })
      await copyTree(fromPath, toPath, exclude)
      continue
    }

    if (!entryStat.isFile()) continue

    await mkdir(path.dirname(toPath), { recursive: true })
    await copyFile(fromPath, toPath)
  }
}

function getDefaultShadowDir(projectDir: string): string {
  const normalized = path.resolve(projectDir)
  const hash = createHash('sha1').update(normalized).digest('hex').slice(0, 16)
  return path.join(os.homedir(), '.ava', 'snapshots', hash)
}

export class ShadowSnapshotManager {
  private readonly shadowDir: string
  private readonly manifestPath: string
  private initialized = false

  constructor(
    private projectDir: string,
    snapshotDir?: string
  ) {
    this.projectDir = path.resolve(projectDir)
    this.shadowDir = snapshotDir ? path.resolve(snapshotDir) : getDefaultShadowDir(this.projectDir)
    this.manifestPath = path.join(this.shadowDir, MANIFEST_NAME)
  }

  async init(): Promise<void> {
    if (this.initialized) return

    await mkdir(this.shadowDir, { recursive: true })

    const gitDir = path.join(this.shadowDir, '.git')
    if (!(await exists(gitDir))) {
      await this.runGit(['init'])
      await this.runGit(['config', 'user.email', 'ava-shadow@local'])
      await this.runGit(['config', 'user.name', 'AVA Shadow'])
    }

    if (!(await exists(this.manifestPath))) {
      await writeFile(this.manifestPath, '[]', 'utf8')
    }

    this.initialized = true
  }

  async take(sessionId: string, message: string): Promise<Snapshot> {
    await this.init()

    await this.syncProjectToShadow()
    await this.runGit(['add', '-A'])
    await this.runGit(['commit', '--allow-empty', '-m', message])

    const commitHash = await this.runGit(['rev-parse', 'HEAD'])
    const snapshot: Snapshot = {
      id: randomUUID(),
      sessionId,
      timestamp: Date.now(),
      message,
      commitHash,
    }

    const snapshots = await this.readManifest()
    snapshots.push(snapshot)
    await this.writeManifest(snapshots)
    return snapshot
  }

  async list(sessionId: string): Promise<Snapshot[]> {
    await this.init()
    const snapshots = await this.readManifest()
    return snapshots
      .filter((snapshot) => snapshot.sessionId === sessionId)
      .sort((a, b) => b.timestamp - a.timestamp)
  }

  async restore(snapshotId: string): Promise<void> {
    await this.init()
    const snapshots = await this.readManifest()
    const snapshot = snapshots.find((item) => item.id === snapshotId)
    if (!snapshot) {
      throw new Error(`Snapshot '${snapshotId}' not found.`)
    }

    await this.runGit(['checkout', snapshot.commitHash, '--', '.'])
    await this.syncShadowToProject()
  }

  async prune(keepPerSession = 10): Promise<number> {
    await this.init()
    const snapshots = await this.readManifest()
    const bySession = new Map<string, Snapshot[]>()

    for (const snapshot of snapshots) {
      const list = bySession.get(snapshot.sessionId)
      if (list) {
        list.push(snapshot)
      } else {
        bySession.set(snapshot.sessionId, [snapshot])
      }
    }

    const kept: Snapshot[] = []
    for (const sessionSnapshots of bySession.values()) {
      const sorted = [...sessionSnapshots].sort((a, b) => b.timestamp - a.timestamp)
      kept.push(...sorted.slice(0, Math.max(0, keepPerSession)))
    }

    const removed = snapshots.length - kept.length
    if (removed > 0) {
      await this.writeManifest(kept.sort((a, b) => a.timestamp - b.timestamp))
    }
    return removed
  }

  private async runGit(args: string[]): Promise<string> {
    const output = await dispatchCompute<{ stdout: string }>(
      'shadow_snapshot_git_exec',
      { cwd: this.shadowDir, args },
      async () => {
        const { stdout } = await execFileAsync('git', ['-C', this.shadowDir, ...args], {
          maxBuffer: 10 * 1024 * 1024,
        })
        return { stdout: stdout.toString().trim() }
      }
    )
    return output.stdout.trim()
  }

  private async readManifest(): Promise<Snapshot[]> {
    const raw = await readFile(this.manifestPath, 'utf8')
    const parsed = JSON.parse(raw) as unknown
    if (!Array.isArray(parsed)) return []

    return parsed.filter((item): item is Snapshot => {
      if (typeof item !== 'object' || item === null) return false
      const candidate = item as Snapshot
      return (
        typeof candidate.id === 'string' &&
        typeof candidate.sessionId === 'string' &&
        typeof candidate.timestamp === 'number' &&
        typeof candidate.message === 'string' &&
        typeof candidate.commitHash === 'string'
      )
    })
  }

  private async writeManifest(snapshots: Snapshot[]): Promise<void> {
    await writeFile(this.manifestPath, JSON.stringify(snapshots, null, 2), 'utf8')
  }

  private async syncProjectToShadow(): Promise<void> {
    await clearDirectory(this.shadowDir, new Set(['.git', MANIFEST_NAME]))
    await copyTree(this.projectDir, this.shadowDir, new Set(['.git']))
  }

  private async syncShadowToProject(): Promise<void> {
    await clearDirectory(this.projectDir, new Set(['.git']))
    await copyTree(this.shadowDir, this.projectDir, new Set(['.git', MANIFEST_NAME]))
  }
}
