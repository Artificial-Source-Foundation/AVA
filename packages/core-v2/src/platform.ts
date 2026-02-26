/**
 * Platform abstraction layer.
 *
 * Never use `node:fs` or `node:child_process` directly — always go through
 * `getPlatform()`. Platform implementations live in `packages/platform-*`.
 */

// ─── File System ─────────────────────────────────────────────────────────────

export interface FileStat {
  isFile: boolean
  isDirectory: boolean
  size: number
  mtime: number
}

export interface DirEntry {
  name: string
  isFile: boolean
  isDirectory: boolean
}

export interface IFileSystem {
  readFile(path: string): Promise<string>
  readBinary(path: string, limit?: number): Promise<Uint8Array>
  writeFile(path: string, content: string): Promise<void>
  writeBinary(path: string, content: Uint8Array): Promise<void>
  readDir(path: string): Promise<string[]>
  readDirWithTypes(path: string): Promise<DirEntry[]>
  stat(path: string): Promise<FileStat>
  exists(path: string): Promise<boolean>
  isFile(path: string): Promise<boolean>
  isDirectory(path: string): Promise<boolean>
  mkdir(path: string): Promise<void>
  remove(path: string): Promise<void>
  glob(pattern: string, cwd: string): Promise<string[]>
  realpath(path: string): Promise<string>
}

// ─── Shell ───────────────────────────────────────────────────────────────────

export interface ExecOptions {
  cwd?: string
  env?: Record<string, string>
  timeout?: number
}

export interface ExecResult {
  stdout: string
  stderr: string
  exitCode: number
}

export interface SpawnOptions {
  cwd?: string
  env?: Record<string, string>
  inactivityTimeout?: number
  killProcessGroup?: boolean
}

export interface ChildProcess {
  pid?: number
  stdin: { write(data: string): void; end(): void }
  stdout: { on(event: 'data', cb: (data: string) => void): void }
  stderr: { on(event: 'data', cb: (data: string) => void): void }
  kill(signal?: string): void
  wait(): Promise<ExecResult>
}

export interface IShell {
  exec(command: string, options?: ExecOptions): Promise<ExecResult>
  spawn(command: string, args: string[], options?: SpawnOptions): ChildProcess
}

// ─── PTY ─────────────────────────────────────────────────────────────────────

export interface PTYOptions {
  cols?: number
  rows?: number
  cwd?: string
  env?: Record<string, string>
}

export interface PTYProcess {
  pid: number
  onData(cb: (data: string) => void): void
  onExit(cb: (exitCode: number) => void): void
  write(data: string): void
  resize(cols: number, rows: number): void
  kill(signal?: string): void
  wait(): Promise<ExecResult>
}

export interface IPTY {
  isSupported(): boolean
  spawn(command: string, args: string[], options?: PTYOptions): PTYProcess
}

// ─── Credentials ─────────────────────────────────────────────────────────────

export interface ICredentialStore {
  get(key: string): Promise<string | null>
  set(key: string, value: string): Promise<void>
  delete(key: string): Promise<void>
  has(key: string): Promise<boolean>
}

// ─── Database ────────────────────────────────────────────────────────────────

export interface Migration {
  version: number
  name: string
  up: string
  down?: string
}

export interface IDatabase {
  query<T>(sql: string, params?: unknown[]): Promise<T[]>
  execute(sql: string, params?: unknown[]): Promise<void>
  migrate(migrations: Migration[]): Promise<void>
  close(): Promise<void>
}

// ─── Platform Provider ───────────────────────────────────────────────────────

export interface IPlatformProvider {
  readonly fs: IFileSystem
  readonly shell: IShell
  readonly credentials: ICredentialStore
  readonly database: IDatabase
  readonly pty?: IPTY
}

// ─── Singleton ───────────────────────────────────────────────────────────────

let _platform: IPlatformProvider | null = null

export function getPlatform(): IPlatformProvider {
  if (!_platform) {
    throw new Error('Platform not initialized. Call setPlatform() first.')
  }
  return _platform
}

export function setPlatform(platform: IPlatformProvider): void {
  _platform = platform
}
