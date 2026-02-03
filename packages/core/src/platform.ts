/**
 * Platform Abstraction Layer
 * Interfaces for platform-specific implementations (Tauri, Node.js)
 */

// ============================================================================
// File System Types
// ============================================================================

/** File/directory statistics */
export interface FileStat {
  isFile: boolean
  isDirectory: boolean
  size: number
  /** Modification time in milliseconds since epoch */
  mtime: number
}

/** Directory entry with type info */
export interface DirEntry {
  name: string
  isFile: boolean
  isDirectory: boolean
}

// ============================================================================
// Shell Types
// ============================================================================

/** Options for shell command execution */
export interface ExecOptions {
  cwd?: string
  env?: Record<string, string>
  timeout?: number
}

/** Options for spawning processes */
export interface SpawnOptions {
  cwd?: string
  env?: Record<string, string>
  /** Inactivity timeout in ms - kill if no output for this duration */
  inactivityTimeout?: number
  /** Kill entire process group on timeout/abort (Unix only) */
  killProcessGroup?: boolean
}

/** Result of shell command execution */
export interface ExecResult {
  stdout: string
  stderr: string
  exitCode: number
}

/** Child process handle for spawned commands */
export interface ChildProcess {
  readonly pid: number | undefined
  readonly stdin: WritableStream<Uint8Array> | null
  readonly stdout: ReadableStream<Uint8Array> | null
  readonly stderr: ReadableStream<Uint8Array> | null
  kill(): void
  wait(): Promise<ExecResult>
}

/** File system operations */
export interface IFileSystem {
  /** Read file contents as string */
  readFile(path: string): Promise<string>

  /** Read file as binary, optionally limited to first N bytes */
  readBinary(path: string, limit?: number): Promise<Uint8Array>

  /** Write string content to file */
  writeFile(path: string, content: string): Promise<void>

  /** Write binary content to file */
  writeBinary(path: string, content: Uint8Array): Promise<void>

  /** Read directory entries (names only) */
  readDir(path: string): Promise<string[]>

  /** Read directory entries with type info */
  readDirWithTypes(path: string): Promise<DirEntry[]>

  /** Get file/directory stats */
  stat(path: string): Promise<FileStat>

  /** Check if path exists */
  exists(path: string): Promise<boolean>

  /** Check if path is a file */
  isFile(path: string): Promise<boolean>

  /** Check if path is a directory */
  isDirectory(path: string): Promise<boolean>

  /** Create directory (recursive) */
  mkdir(path: string): Promise<void>

  /** Remove file or directory */
  remove(path: string): Promise<void>

  /** Glob pattern matching */
  glob(pattern: string, cwd: string): Promise<string[]>
}

/** Shell command execution */
export interface IShell {
  /** Execute command and wait for result */
  exec(command: string, options?: ExecOptions): Promise<ExecResult>

  /** Spawn command with streaming I/O */
  spawn(command: string, args: string[], options?: SpawnOptions): ChildProcess
}

/** Credential/secret storage */
export interface ICredentialStore {
  /** Get credential by key */
  get(key: string): Promise<string | null>

  /** Set credential */
  set(key: string, value: string): Promise<void>

  /** Delete credential */
  delete(key: string): Promise<void>

  /** Check if credential exists */
  has(key: string): Promise<boolean>
}

/** Database operations */
export interface IDatabase {
  /** Execute query and return results */
  query<T>(sql: string, params?: unknown[]): Promise<T[]>

  /** Execute statement (no results) */
  execute(sql: string, params?: unknown[]): Promise<void>

  /** Run migrations */
  migrate(migrations: Migration[]): Promise<void>

  /** Close database connection */
  close(): Promise<void>
}

/** Database migration */
export interface Migration {
  version: number
  name: string
  up: string
  down?: string
}

/** Platform provider - factory for platform-specific implementations */
export interface IPlatformProvider {
  readonly fs: IFileSystem
  readonly shell: IShell
  readonly credentials: ICredentialStore
  readonly database: IDatabase
}

/** Global platform instance - set by platform package */
let _platform: IPlatformProvider | null = null

/** Get the current platform provider */
export function getPlatform(): IPlatformProvider {
  if (!_platform) {
    throw new Error('Platform not initialized. Call setPlatform() first.')
  }
  return _platform
}

/** Set the platform provider (called by platform-tauri or platform-node) */
export function setPlatform(platform: IPlatformProvider): void {
  _platform = platform
}
