/**
 * @estela/platform-node
 * Node.js platform implementations for CLI
 */

import type { IPlatformProvider, IPTY } from '@estela/core'
import { NodeCredentialStore } from './credentials.js'
import { NodeDatabase } from './database.js'
import { NodeFileSystem } from './fs.js'
import { createNodePTY } from './pty.js'
import { NodeShell } from './shell.js'

export { NodeCredentialStore } from './credentials.js'
export { NodeDatabase } from './database.js'
export { NodeFileSystem } from './fs.js'
export type { PTYImplementation } from './pty.js'
export { createNodePTY, getPTYImplementationName, NodePTY } from './pty.js'
export { NodeShell } from './shell.js'

/** Options for creating Node.js platform */
export interface NodePlatformOptions {
  /** Database path for SQLite storage */
  dbPath: string
  /** Enable PTY support for interactive commands (default: true) */
  enablePty?: boolean
}

/** Create Node.js platform provider */
export function createNodePlatform(dbPath: string): IPlatformProvider {
  return {
    fs: new NodeFileSystem(),
    shell: new NodeShell(),
    credentials: new NodeCredentialStore(),
    database: new NodeDatabase(dbPath),
    // PTY not included by default - use createNodePlatformAsync for PTY support
  }
}

/** Create Node.js platform provider with async PTY initialization */
export async function createNodePlatformAsync(
  options: NodePlatformOptions
): Promise<IPlatformProvider> {
  let pty: IPTY | undefined

  if (options.enablePty !== false) {
    const nodePty = await createNodePTY()
    pty = nodePty ?? undefined
  }

  return {
    fs: new NodeFileSystem(),
    shell: new NodeShell(),
    credentials: new NodeCredentialStore(),
    database: new NodeDatabase(options.dbPath),
    pty,
  }
}
