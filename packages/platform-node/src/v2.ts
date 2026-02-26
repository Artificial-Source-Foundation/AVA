/**
 * @ava/platform-node/v2
 *
 * Node.js platform implementations using core-v2 types.
 * Structurally identical to the original exports — the platform interfaces
 * between @ava/core and @ava/core-v2 are aligned.
 */

import type { IPlatformProvider } from '@ava/core-v2/platform'
import { NodeCredentialStore } from './credentials.js'
import { NodeDatabase } from './database.js'
import { NodeFileSystem } from './fs.js'
import { NodeShell } from './shell.js'

export { NodeCredentialStore } from './credentials.js'
export { NodeDatabase } from './database.js'
export { NodeFileSystem } from './fs.js'
export { NodeShell } from './shell.js'

/** Create Node.js platform provider (core-v2 compatible) */
export function createNodePlatform(dbPath: string): IPlatformProvider {
  return {
    fs: new NodeFileSystem(),
    shell: new NodeShell(),
    credentials: new NodeCredentialStore(),
    database: new NodeDatabase(dbPath),
  }
}
