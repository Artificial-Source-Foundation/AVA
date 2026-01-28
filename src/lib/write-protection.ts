/**
 * Delta9 Concurrent Write Protection (D-2)
 *
 * Protects against concurrent writes to the same file:
 * - File versioning using content hashes
 * - Write queuing per file
 * - Optimistic locking with conflict detection
 * - Automatic retry with backoff
 */

import * as crypto from 'node:crypto'
import { getNamedLogger } from './logger.js'

const log = getNamedLogger('write-protection')

// =============================================================================
// Types
// =============================================================================

/** Write request in the queue */
export interface WriteRequest {
  /** Unique request ID */
  id: string
  /** File path */
  filePath: string
  /** Content to write */
  content: string
  /** Expected version (hash of content before write) */
  expectedVersion?: string
  /** Requester info */
  requester: {
    sessionId: string
    agentId?: string
    taskId?: string
  }
  /** When the request was queued */
  queuedAt: number
  /** Number of retry attempts */
  retryCount: number
  /** Maximum retries */
  maxRetries: number
  /** Callback when write completes */
  resolve: (result: WriteResult) => void
  /** Callback when write fails */
  reject: (error: Error) => void
}

/** Result of a write operation */
export interface WriteResult {
  /** Whether the write succeeded */
  success: boolean
  /** New version hash after write */
  newVersion?: string
  /** Error message if failed */
  error?: string
  /** Whether a conflict was detected */
  conflict?: boolean
  /** Details about the conflict */
  conflictDetails?: {
    expectedVersion: string
    actualVersion: string
    modifiedBy?: string
    modifiedAt?: number
  }
}

/** File state tracking */
export interface FileState {
  /** Current content hash */
  version: string
  /** Last modification time */
  modifiedAt: number
  /** Last modifier */
  modifiedBy?: string
  /** Write in progress */
  writeInProgress: boolean
}

/** Write protection configuration */
export interface WriteProtectionConfig {
  /** Maximum concurrent writes per file (default: 1) */
  maxConcurrentPerFile?: number
  /** Maximum queue length per file (default: 10) */
  maxQueueLength?: number
  /** Base retry delay in ms (default: 100) */
  retryDelayMs?: number
  /** Maximum retry delay in ms (default: 5000) */
  maxRetryDelayMs?: number
  /** Default max retries (default: 3) */
  defaultMaxRetries?: number
  /** Callback for write events */
  onWrite?: (filePath: string, result: WriteResult) => void
  /** Callback for conflicts */
  onConflict?: (filePath: string, request: WriteRequest, actualVersion: string) => void
}

// =============================================================================
// Write Protection Manager
// =============================================================================

export class WriteProtectionManager {
  private fileStates: Map<string, FileState> = new Map()
  private writeQueues: Map<string, WriteRequest[]> = new Map()
  private processing: Set<string> = new Set()
  private requestCounter = 0
  private config: Required<WriteProtectionConfig>

  constructor(config: WriteProtectionConfig = {}) {
    this.config = {
      maxConcurrentPerFile: config.maxConcurrentPerFile ?? 1,
      maxQueueLength: config.maxQueueLength ?? 10,
      retryDelayMs: config.retryDelayMs ?? 100,
      maxRetryDelayMs: config.maxRetryDelayMs ?? 5000,
      defaultMaxRetries: config.defaultMaxRetries ?? 3,
      onWrite: config.onWrite ?? (() => {}),
      onConflict: config.onConflict ?? (() => {}),
    }
  }

  // ===========================================================================
  // Public API
  // ===========================================================================

  /**
   * Request a write with conflict protection
   */
  async write(
    filePath: string,
    content: string,
    options: {
      expectedVersion?: string
      sessionId: string
      agentId?: string
      taskId?: string
      maxRetries?: number
    }
  ): Promise<WriteResult> {
    const queue = this.getOrCreateQueue(filePath)

    // Check queue limit
    if (queue.length >= this.config.maxQueueLength) {
      log.warn(`Write queue full for ${filePath}`)
      return {
        success: false,
        error: `Write queue full for ${filePath} (${queue.length} pending)`,
      }
    }

    return new Promise((resolve, reject) => {
      const request: WriteRequest = {
        id: `write_${++this.requestCounter}_${Date.now()}`,
        filePath,
        content,
        expectedVersion: options.expectedVersion,
        requester: {
          sessionId: options.sessionId,
          agentId: options.agentId,
          taskId: options.taskId,
        },
        queuedAt: Date.now(),
        retryCount: 0,
        maxRetries: options.maxRetries ?? this.config.defaultMaxRetries,
        resolve,
        reject,
      }

      queue.push(request)
      log.debug(`Queued write request ${request.id} for ${filePath}`)

      // Process the queue
      this.processQueue(filePath)
    })
  }

  /**
   * Register a file's current state (call after reading)
   */
  registerVersion(filePath: string, content: string, modifiedBy?: string): string {
    const version = this.computeHash(content)

    this.fileStates.set(filePath, {
      version,
      modifiedAt: Date.now(),
      modifiedBy,
      writeInProgress: false,
    })

    log.debug(`Registered version for ${filePath}: ${version.slice(0, 8)}...`)
    return version
  }

  /**
   * Get current known version of a file
   */
  getVersion(filePath: string): string | undefined {
    return this.fileStates.get(filePath)?.version
  }

  /**
   * Check if a write would conflict
   */
  checkConflict(
    filePath: string,
    expectedVersion: string
  ): { conflict: boolean; currentVersion?: string } {
    const state = this.fileStates.get(filePath)
    if (!state) {
      return { conflict: false }
    }

    if (state.version !== expectedVersion) {
      return {
        conflict: true,
        currentVersion: state.version,
      }
    }

    return { conflict: false }
  }

  /**
   * Check if a file has a write in progress
   */
  isWriteInProgress(filePath: string): boolean {
    return this.processing.has(filePath)
  }

  /**
   * Get queue status for a file
   */
  getQueueStatus(filePath: string): {
    queueLength: number
    processing: boolean
    oldestRequestAge?: number
  } {
    const queue = this.writeQueues.get(filePath) || []
    const oldestRequest = queue[0]

    return {
      queueLength: queue.length,
      processing: this.processing.has(filePath),
      oldestRequestAge: oldestRequest ? Date.now() - oldestRequest.queuedAt : undefined,
    }
  }

  /**
   * Clear all state (for testing)
   */
  clear(): void {
    this.fileStates.clear()
    this.writeQueues.clear()
    this.processing.clear()
  }

  /**
   * Get statistics
   */
  getStats(): {
    trackedFiles: number
    totalQueuedWrites: number
    filesWithPendingWrites: number
  } {
    let totalQueued = 0
    let filesWithPending = 0

    for (const queue of this.writeQueues.values()) {
      if (queue.length > 0) {
        totalQueued += queue.length
        filesWithPending++
      }
    }

    return {
      trackedFiles: this.fileStates.size,
      totalQueuedWrites: totalQueued,
      filesWithPendingWrites: filesWithPending,
    }
  }

  // ===========================================================================
  // Internal Methods
  // ===========================================================================

  /**
   * Get or create a write queue for a file
   */
  private getOrCreateQueue(filePath: string): WriteRequest[] {
    let queue = this.writeQueues.get(filePath)
    if (!queue) {
      queue = []
      this.writeQueues.set(filePath, queue)
    }
    return queue
  }

  /**
   * Process the write queue for a file
   */
  private async processQueue(filePath: string): Promise<void> {
    // Only one processor per file
    if (this.processing.has(filePath)) {
      return
    }

    const queue = this.writeQueues.get(filePath)
    if (!queue || queue.length === 0) {
      return
    }

    this.processing.add(filePath)

    try {
      while (queue.length > 0) {
        const request = queue[0]

        const result = await this.executeWrite(request)

        if (result.success || !result.conflict || request.retryCount >= request.maxRetries) {
          // Complete the request
          queue.shift()
          request.resolve(result)
          this.config.onWrite(filePath, result)
        } else {
          // Retry with backoff
          request.retryCount++
          const delay = Math.min(
            this.config.retryDelayMs * Math.pow(2, request.retryCount - 1),
            this.config.maxRetryDelayMs
          )

          log.debug(`Retrying write ${request.id} in ${delay}ms (attempt ${request.retryCount})`)
          await this.sleep(delay)
        }
      }
    } finally {
      this.processing.delete(filePath)
    }
  }

  /**
   * Execute a single write request
   */
  private async executeWrite(request: WriteRequest): Promise<WriteResult> {
    const { filePath, content, expectedVersion } = request

    // Check for version conflict
    if (expectedVersion) {
      const state = this.fileStates.get(filePath)
      if (state && state.version !== expectedVersion) {
        log.warn(`Version conflict detected for ${filePath}`)

        this.config.onConflict(filePath, request, state.version)

        return {
          success: false,
          conflict: true,
          conflictDetails: {
            expectedVersion,
            actualVersion: state.version,
            modifiedBy: state.modifiedBy,
            modifiedAt: state.modifiedAt,
          },
        }
      }
    }

    // Simulate the write (in real implementation, this would write to disk)
    // For now, we just update the state
    const newVersion = this.computeHash(content)

    this.fileStates.set(filePath, {
      version: newVersion,
      modifiedAt: Date.now(),
      modifiedBy: request.requester.agentId || request.requester.sessionId,
      writeInProgress: false,
    })

    log.debug(`Write completed for ${filePath}: ${newVersion.slice(0, 8)}...`)

    return {
      success: true,
      newVersion,
    }
  }

  /**
   * Compute hash of content
   */
  private computeHash(content: string): string {
    return crypto.createHash('sha256').update(content).digest('hex')
  }

  /**
   * Sleep for a duration
   */
  private sleep(ms: number): Promise<void> {
    return new Promise((resolve) => setTimeout(resolve, ms))
  }
}

// =============================================================================
// Factory Functions
// =============================================================================

/** Singleton instance */
let defaultManager: WriteProtectionManager | null = null

/**
 * Get or create the default write protection manager
 */
export function getWriteProtectionManager(config?: WriteProtectionConfig): WriteProtectionManager {
  if (!defaultManager) {
    defaultManager = new WriteProtectionManager(config)
  }
  return defaultManager
}

/**
 * Reset the default manager (for testing)
 */
export function resetWriteProtectionManager(): void {
  defaultManager = null
}

/**
 * Create a new write protection manager
 */
export function createWriteProtectionManager(
  config?: WriteProtectionConfig
): WriteProtectionManager {
  return new WriteProtectionManager(config)
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Wrapper for protected file writes
 */
export async function protectedWrite(
  filePath: string,
  content: string,
  options: {
    sessionId: string
    agentId?: string
    taskId?: string
    expectedVersion?: string
    maxRetries?: number
  }
): Promise<WriteResult> {
  const manager = getWriteProtectionManager()
  return manager.write(filePath, content, options)
}

/**
 * Wrapper for registering file versions
 */
export function registerFileVersion(
  filePath: string,
  content: string,
  modifiedBy?: string
): string {
  const manager = getWriteProtectionManager()
  return manager.registerVersion(filePath, content, modifiedBy)
}

/**
 * Check if a file write would conflict
 */
export function wouldConflict(filePath: string, expectedVersion: string): boolean {
  const manager = getWriteProtectionManager()
  return manager.checkConflict(filePath, expectedVersion).conflict
}
