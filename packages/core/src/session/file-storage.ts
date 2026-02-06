/**
 * File-Based Session Storage
 * Persists sessions to disk at ~/.estela/sessions/
 *
 * Directory structure:
 *   ~/.estela/sessions/
 *     ├── <sessionId>.json          # Session state
 *     └── checkpoints/
 *         └── <sessionId>/
 *             └── <checkpointId>.json
 */

import { mkdir, readdir, readFile, rm, writeFile } from 'node:fs/promises'
import { join } from 'node:path'
import type { Checkpoint, CheckpointMeta, SerializedSessionState, SessionStorage } from './types.js'

// ============================================================================
// Constants
// ============================================================================

const SESSION_FILE_EXT = '.json'
const CHECKPOINTS_DIR = 'checkpoints'

// ============================================================================
// File-Based Storage
// ============================================================================

/**
 * File-based session storage implementation.
 * Stores session JSON files in a configurable directory.
 */
export class FileSessionStorage implements SessionStorage {
  private readonly sessionsDir: string
  private initialized = false

  constructor(sessionsDir: string) {
    this.sessionsDir = sessionsDir
  }

  // ==========================================================================
  // Initialization
  // ==========================================================================

  /**
   * Ensure storage directories exist
   */
  private async ensureInit(): Promise<void> {
    if (this.initialized) return

    await mkdir(this.sessionsDir, { recursive: true })
    await mkdir(join(this.sessionsDir, CHECKPOINTS_DIR), { recursive: true })
    this.initialized = true
  }

  // ==========================================================================
  // Session Operations
  // ==========================================================================

  async save(session: SerializedSessionState): Promise<void> {
    await this.ensureInit()

    const filePath = this.sessionPath(session.id)
    const data = JSON.stringify(session, null, 2)
    await writeFile(filePath, data, 'utf-8')
  }

  async load(sessionId: string): Promise<SerializedSessionState | null> {
    await this.ensureInit()

    const filePath = this.sessionPath(sessionId)
    try {
      const data = await readFile(filePath, 'utf-8')
      return JSON.parse(data) as SerializedSessionState
    } catch (err) {
      if (isNotFoundError(err)) return null
      throw err
    }
  }

  async delete(sessionId: string): Promise<void> {
    await this.ensureInit()

    // Delete session file
    const filePath = this.sessionPath(sessionId)
    try {
      await rm(filePath)
    } catch (err) {
      if (!isNotFoundError(err)) throw err
    }

    // Delete checkpoint directory
    const checkpointDir = this.checkpointDir(sessionId)
    try {
      await rm(checkpointDir, { recursive: true })
    } catch (err) {
      if (!isNotFoundError(err)) throw err
    }
  }

  async list(): Promise<string[]> {
    await this.ensureInit()

    try {
      const files = await readdir(this.sessionsDir)
      return files
        .filter((f) => f.endsWith(SESSION_FILE_EXT) && !f.startsWith('.'))
        .map((f) => f.slice(0, -SESSION_FILE_EXT.length))
    } catch {
      return []
    }
  }

  // ==========================================================================
  // Checkpoint Operations
  // ==========================================================================

  async saveCheckpoint(sessionId: string, checkpoint: Checkpoint): Promise<void> {
    await this.ensureInit()

    const dir = this.checkpointDir(sessionId)
    await mkdir(dir, { recursive: true })

    const filePath = join(dir, `${checkpoint.id}${SESSION_FILE_EXT}`)
    const data = JSON.stringify(checkpoint, null, 2)
    await writeFile(filePath, data, 'utf-8')
  }

  async loadCheckpoint(sessionId: string, checkpointId: string): Promise<Checkpoint | null> {
    await this.ensureInit()

    const filePath = join(this.checkpointDir(sessionId), `${checkpointId}${SESSION_FILE_EXT}`)
    try {
      const data = await readFile(filePath, 'utf-8')
      return JSON.parse(data) as Checkpoint
    } catch (err) {
      if (isNotFoundError(err)) return null
      throw err
    }
  }

  async deleteCheckpoint(sessionId: string, checkpointId: string): Promise<void> {
    await this.ensureInit()

    const filePath = join(this.checkpointDir(sessionId), `${checkpointId}${SESSION_FILE_EXT}`)
    try {
      await rm(filePath)
    } catch (err) {
      if (!isNotFoundError(err)) throw err
    }
  }

  async listCheckpoints(sessionId: string): Promise<CheckpointMeta[]> {
    await this.ensureInit()

    const dir = this.checkpointDir(sessionId)
    let files: string[]
    try {
      files = await readdir(dir)
    } catch {
      return []
    }

    const metas: CheckpointMeta[] = []
    for (const file of files) {
      if (!file.endsWith(SESSION_FILE_EXT)) continue

      try {
        const data = await readFile(join(dir, file), 'utf-8')
        const checkpoint = JSON.parse(data) as Checkpoint
        metas.push({
          id: checkpoint.id,
          timestamp: checkpoint.timestamp,
          description: checkpoint.description,
          messageCount: checkpoint.messageCount,
        })
      } catch {
        // Skip corrupted checkpoint files
      }
    }

    return metas.sort((a, b) => a.timestamp - b.timestamp)
  }

  // ==========================================================================
  // Bulk Operations
  // ==========================================================================

  /**
   * Load all session metadata without full message content.
   * More efficient than loading full sessions for listing.
   */
  async listSessionMetas(): Promise<SessionMetaFile[]> {
    await this.ensureInit()

    const ids = await this.list()
    const metas: SessionMetaFile[] = []

    for (const id of ids) {
      try {
        const session = await this.load(id)
        if (session) {
          metas.push({
            id: session.id,
            name: session.name,
            messageCount: session.messages.length,
            workingDirectory: session.workingDirectory,
            createdAt: session.createdAt,
            updatedAt: session.updatedAt,
            status: session.status,
            parentId: session.parentId,
          })
        }
      } catch {
        // Skip corrupted session files
      }
    }

    return metas.sort((a, b) => b.updatedAt - a.updatedAt)
  }

  /**
   * Get total size of session storage on disk (bytes)
   */
  async getStorageSize(): Promise<number> {
    await this.ensureInit()

    let totalSize = 0
    try {
      const files = await readdir(this.sessionsDir)
      for (const file of files) {
        if (!file.endsWith(SESSION_FILE_EXT)) continue
        try {
          const data = await readFile(join(this.sessionsDir, file), 'utf-8')
          totalSize += Buffer.byteLength(data)
        } catch {
          // Skip unreadable files
        }
      }
    } catch {
      // Directory doesn't exist
    }

    return totalSize
  }

  // ==========================================================================
  // Path Helpers
  // ==========================================================================

  private sessionPath(sessionId: string): string {
    return join(this.sessionsDir, `${sessionId}${SESSION_FILE_EXT}`)
  }

  private checkpointDir(sessionId: string): string {
    return join(this.sessionsDir, CHECKPOINTS_DIR, sessionId)
  }

  /**
   * Get the base sessions directory path
   */
  getStoragePath(): string {
    return this.sessionsDir
  }
}

// ============================================================================
// Types
// ============================================================================

/**
 * Session metadata from file storage (lightweight, no messages)
 */
export interface SessionMetaFile {
  id: string
  name?: string
  messageCount: number
  workingDirectory: string
  createdAt: number
  updatedAt: number
  status: string
  parentId?: string
}

// ============================================================================
// Helpers
// ============================================================================

function isNotFoundError(err: unknown): boolean {
  return err instanceof Error && 'code' in err && (err as NodeJS.ErrnoException).code === 'ENOENT'
}

// ============================================================================
// Factory
// ============================================================================

/**
 * Create a file-based session storage at the default location.
 *
 * @param baseDir - Base directory (defaults to ~/.estela)
 * @returns FileSessionStorage instance
 */
export function createFileSessionStorage(baseDir?: string): FileSessionStorage {
  const dir = baseDir ?? join(getDefaultBaseDir(), 'sessions')
  return new FileSessionStorage(dir)
}

/**
 * Get the default Estela base directory
 */
function getDefaultBaseDir(): string {
  const home = process.env.HOME ?? process.env.USERPROFILE ?? '.'
  return join(home, '.estela')
}
