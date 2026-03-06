import { randomUUID } from 'node:crypto'
import type { IFileSystem } from '@ava/core-v2/platform'

export interface SandboxedChange {
  id: string
  file: string
  type: 'create' | 'modify' | 'delete'
  originalContent: string | null
  newContent: string
  diff: string
  timestamp: number
}

function toLines(value: string): string[] {
  if (!value) return []
  return value.split('\n')
}

function createUnifiedDiff(change: {
  file: string
  originalContent: string | null
  newContent: string
  type: SandboxedChange['type']
}): string {
  const oldFile = change.type === 'create' ? '/dev/null' : `a/${change.file}`
  const newFile = change.type === 'delete' ? '/dev/null' : `b/${change.file}`
  const oldLines = toLines(change.originalContent ?? '')
  const newLines = toLines(change.newContent)

  const diffLines = [
    `--- ${oldFile}`,
    `+++ ${newFile}`,
    `@@ -1,${oldLines.length} +1,${newLines.length} @@`,
    ...oldLines.map((line) => `-${line}`),
    ...newLines.map((line) => `+${line}`),
  ]

  return diffLines.join('\n')
}

export class DiffSandbox {
  private pending: Map<string, SandboxedChange> = new Map()

  constructor(private readonly fs: Pick<IFileSystem, 'writeFile' | 'remove'>) {}

  stage(change: Omit<SandboxedChange, 'id' | 'timestamp' | 'diff'>): SandboxedChange {
    const staged: SandboxedChange = {
      ...change,
      id: randomUUID(),
      timestamp: Date.now(),
      diff: createUnifiedDiff(change),
    }

    this.pending.set(staged.id, staged)
    return staged
  }

  getPending(): SandboxedChange[] {
    return [...this.pending.values()].sort((a, b) => a.timestamp - b.timestamp)
  }

  async apply(id: string): Promise<void> {
    const change = this.pending.get(id)
    if (!change) return

    if (change.type === 'delete') {
      await this.fs.remove(change.file)
    } else {
      await this.fs.writeFile(change.file, change.newContent)
    }

    this.pending.delete(id)
  }

  async applyAll(): Promise<void> {
    const ids = this.getPending().map((change) => change.id)
    for (const id of ids) {
      await this.apply(id)
    }
  }

  reject(id: string): void {
    this.pending.delete(id)
  }

  clear(): void {
    this.pending.clear()
  }
}
