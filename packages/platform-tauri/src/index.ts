/**
 * @ava/platform-tauri
 * Tauri platform implementations
 */

import type { IPlatformProvider } from '@ava/core'
import { TauriCredentialStore } from './credentials.js'
import { TauriDatabase } from './database.js'
import { TauriFileSystem } from './fs.js'
import { TauriShell } from './shell.js'

export { TauriCredentialStore } from './credentials.js'
export { TauriDatabase } from './database.js'
export { TauriFileSystem } from './fs.js'
export { TauriShell } from './shell.js'

/** Create Tauri platform provider */
export function createTauriPlatform(dbPath: string): IPlatformProvider {
  return {
    fs: new TauriFileSystem(),
    shell: new TauriShell(),
    credentials: new TauriCredentialStore(),
    database: new TauriDatabase(dbPath),
  }
}
