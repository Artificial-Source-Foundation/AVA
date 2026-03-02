/**
 * @ava/platform-tauri
 * Tauri platform implementations
 */

import type { IPlatformProvider, IPTY } from '@ava/core-v2'
import { TauriCredentialStore } from './credentials.js'
import { TauriDatabase } from './database.js'
import { TauriFileSystem } from './fs.js'
import { TauriPTY } from './pty.js'
import { TauriShell } from './shell.js'

export { TauriCredentialStore } from './credentials.js'
export { TauriDatabase } from './database.js'
export { TauriFileSystem } from './fs.js'
export { TauriPTY } from './pty.js'
export { TauriShell } from './shell.js'
export type { FileWatchEvent, FileWatchHandler } from './watcher.js'
export { TauriFileWatcher } from './watcher.js'

/** Options for creating the Tauri platform provider */
export interface TauriPlatformOptions {
  /** Database path for SQLite storage */
  dbPath: string
  /** Enable PTY support (default: true) */
  enablePty?: boolean
}

/** Create Tauri platform provider */
export function createTauriPlatform(dbPath: string): IPlatformProvider {
  const pty = new TauriPTY()
  const ptySupported = pty.isSupported()

  return {
    fs: new TauriFileSystem(),
    shell: new TauriShell(),
    credentials: new TauriCredentialStore(),
    database: new TauriDatabase(dbPath),
    pty: ptySupported ? pty : undefined,
  }
}

/** Create Tauri platform provider with explicit options */
export function createTauriPlatformWithOptions(options: TauriPlatformOptions): IPlatformProvider {
  let pty: IPTY | undefined
  if (options.enablePty !== false) {
    const tauriPty = new TauriPTY()
    if (tauriPty.isSupported()) {
      pty = tauriPty
    }
  }

  return {
    fs: new TauriFileSystem(),
    shell: new TauriShell(),
    credentials: new TauriCredentialStore(),
    database: new TauriDatabase(options.dbPath),
    pty,
  }
}
