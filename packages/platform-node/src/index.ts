/**
 * @estela/platform-node
 * Node.js platform implementations for CLI
 */

import type { IPlatformProvider } from '@estela/core'
import { NodeCredentialStore } from './credentials.js'
import { NodeDatabase } from './database.js'
import { NodeFileSystem } from './fs.js'
import { NodeShell } from './shell.js'

export { NodeCredentialStore } from './credentials.js'
export { NodeDatabase } from './database.js'
export { NodeFileSystem } from './fs.js'
export { NodeShell } from './shell.js'

/** Create Node.js platform provider */
export function createNodePlatform(dbPath: string): IPlatformProvider {
  return {
    fs: new NodeFileSystem(),
    shell: new NodeShell(),
    credentials: new NodeCredentialStore(),
    database: new NodeDatabase(dbPath),
  }
}
