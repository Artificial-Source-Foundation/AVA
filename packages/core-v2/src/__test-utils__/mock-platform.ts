/**
 * Shared mock platform for tool tests.
 *
 * Provides an in-memory file system, shell, credential store, and database.
 */

import type {
  ChildProcess,
  DirEntry,
  ExecResult,
  FileStat,
  ICredentialStore,
  IDatabase,
  IFileSystem,
  IPlatformProvider,
  IShell,
} from '../platform.js'
import { setPlatform } from '../platform.js'

// ─── In-Memory File System ──────────────────────────────────────────────────

export class MockFileSystem implements IFileSystem {
  files = new Map<string, string>()
  binaries = new Map<string, Uint8Array>()
  dirs = new Set<string>(['/'])

  addFile(path: string, content: string): void {
    this.files.set(path, content)
    // Ensure parent dirs exist
    const parts = path.split('/')
    for (let i = 1; i < parts.length; i++) {
      this.dirs.add(parts.slice(0, i).join('/') || '/')
    }
  }

  addBinary(path: string, data: Uint8Array): void {
    this.binaries.set(path, data)
  }

  addDir(path: string): void {
    this.dirs.add(path)
  }

  async readFile(path: string): Promise<string> {
    const content = this.files.get(path)
    if (content === undefined) throw new Error(`ENOENT: ${path}`)
    return content
  }

  async readBinary(path: string, limit?: number): Promise<Uint8Array> {
    const data = this.binaries.get(path)
    if (data) return limit ? data.slice(0, limit) : data
    const text = this.files.get(path)
    if (text === undefined) throw new Error(`ENOENT: ${path}`)
    const encoded = new TextEncoder().encode(text)
    return limit ? encoded.slice(0, limit) : encoded
  }

  async writeFile(path: string, content: string): Promise<void> {
    this.files.set(path, content)
  }

  async writeBinary(path: string, content: Uint8Array): Promise<void> {
    this.binaries.set(path, content)
  }

  async readDir(path: string): Promise<string[]> {
    if (!this.dirs.has(path) && !this.hasDirContents(path)) {
      throw new Error(`ENOENT: ${path}`)
    }
    const entries: string[] = []
    const prefix = path.endsWith('/') ? path : `${path}/`
    const seen = new Set<string>()

    for (const filePath of this.files.keys()) {
      if (filePath.startsWith(prefix)) {
        const rest = filePath.slice(prefix.length)
        const name = rest.split('/')[0]
        if (!seen.has(name)) {
          seen.add(name)
          entries.push(name)
        }
      }
    }
    for (const dirPath of this.dirs) {
      if (dirPath.startsWith(prefix) && dirPath !== path) {
        const rest = dirPath.slice(prefix.length)
        const name = rest.split('/')[0]
        if (!seen.has(name)) {
          seen.add(name)
          entries.push(name)
        }
      }
    }
    return entries.sort()
  }

  async readDirWithTypes(path: string): Promise<DirEntry[]> {
    const names = await this.readDir(path)
    const prefix = path.endsWith('/') ? path : `${path}/`
    return names.map((name) => {
      const fullPath = `${prefix}${name}`
      const isFile = this.files.has(fullPath) || this.binaries.has(fullPath)
      const isDirectory = this.dirs.has(fullPath) || this.hasDirContents(fullPath)
      return { name, isFile: isFile && !isDirectory, isDirectory }
    })
  }

  async stat(path: string): Promise<FileStat> {
    if (this.dirs.has(path) || this.hasDirContents(path)) {
      return { isFile: false, isDirectory: true, size: 0, mtime: Date.now() }
    }
    if (this.files.has(path)) {
      return {
        isFile: true,
        isDirectory: false,
        size: Buffer.byteLength(this.files.get(path)!, 'utf8'),
        mtime: Date.now(),
      }
    }
    if (this.binaries.has(path)) {
      return {
        isFile: true,
        isDirectory: false,
        size: this.binaries.get(path)!.length,
        mtime: Date.now(),
      }
    }
    throw new Error(`ENOENT: ${path}`)
  }

  async exists(path: string): Promise<boolean> {
    return this.files.has(path) || this.dirs.has(path) || this.binaries.has(path)
  }

  async isFile(path: string): Promise<boolean> {
    return this.files.has(path) || this.binaries.has(path)
  }

  async isDirectory(path: string): Promise<boolean> {
    return this.dirs.has(path) || this.hasDirContents(path)
  }

  async mkdir(path: string): Promise<void> {
    this.dirs.add(path)
  }

  async remove(path: string): Promise<void> {
    this.files.delete(path)
    this.binaries.delete(path)
    this.dirs.delete(path)
  }

  async glob(pattern: string, _cwd: string): Promise<string[]> {
    return [...this.files.keys()].filter((f) => f.includes(pattern.replace(/\*/g, '')))
  }

  async realpath(path: string): Promise<string> {
    if (!this.files.has(path) && !this.dirs.has(path) && !this.hasDirContents(path)) {
      throw new Error(`ENOENT: ${path}`)
    }
    return path
  }

  private hasDirContents(path: string): boolean {
    const prefix = path.endsWith('/') ? path : `${path}/`
    for (const key of this.files.keys()) {
      if (key.startsWith(prefix)) return true
    }
    for (const key of this.dirs) {
      if (key.startsWith(prefix) && key !== path) return true
    }
    return false
  }
}

// ─── Mock Shell ─────────────────────────────────────────────────────────────

export class MockShell implements IShell {
  execResults = new Map<string, ExecResult>()
  defaultResult: ExecResult = { stdout: '', stderr: '', exitCode: 0 }

  setResult(command: string, result: ExecResult): void {
    this.execResults.set(command, result)
  }

  async exec(command: string): Promise<ExecResult> {
    return this.execResults.get(command) ?? this.defaultResult
  }

  spawn(command: string, args: string[]): ChildProcess {
    const cmd = `${command} ${args.join(' ')}`
    const result = this.execResults.get(cmd) ?? this.defaultResult
    const encoder = new TextEncoder()

    const stdoutStream = new ReadableStream<Uint8Array>({
      start(controller) {
        if (result.stdout) {
          controller.enqueue(encoder.encode(result.stdout))
        }
        controller.close()
      },
    })

    const stderrStream = new ReadableStream<Uint8Array>({
      start(controller) {
        if (result.stderr) {
          controller.enqueue(encoder.encode(result.stderr))
        }
        controller.close()
      },
    })

    return {
      pid: 123,
      stdin: null,
      stdout: stdoutStream,
      stderr: stderrStream,
      kill() {},
      async wait(): Promise<ExecResult> {
        return result
      },
    }
  }
}

// ─── Mock Credential Store ──────────────────────────────────────────────────

export class MockCredentialStore implements ICredentialStore {
  private store = new Map<string, string>()

  async get(key: string): Promise<string | null> {
    return this.store.get(key) ?? null
  }

  async set(key: string, value: string): Promise<void> {
    this.store.set(key, value)
  }

  async delete(key: string): Promise<void> {
    this.store.delete(key)
  }

  async has(key: string): Promise<boolean> {
    return this.store.has(key)
  }
}

// ─── Mock Database ──────────────────────────────────────────────────────────

export class MockDatabase implements IDatabase {
  async query<T>(): Promise<T[]> {
    return []
  }
  async execute(): Promise<void> {}
  async migrate(): Promise<void> {}
  async close(): Promise<void> {}
}

// ─── Create Mock Platform ───────────────────────────────────────────────────

export interface MockPlatform extends IPlatformProvider {
  readonly fs: MockFileSystem
  readonly shell: MockShell
  readonly credentials: MockCredentialStore
  readonly database: MockDatabase
}

export function createMockPlatform(): MockPlatform {
  return {
    fs: new MockFileSystem(),
    shell: new MockShell(),
    credentials: new MockCredentialStore(),
    database: new MockDatabase(),
  }
}

export function installMockPlatform(): MockPlatform {
  const platform = createMockPlatform()
  setPlatform(platform)
  return platform
}
