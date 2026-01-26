/**
 * Delta9 File Reservation System - Lock Store
 *
 * CAS-based in-memory lock store with:
 * - Atomic lock/unlock operations
 * - TTL-based automatic expiration
 * - Version tracking for Compare-And-Swap
 * - Event emission for monitoring
 */

import * as path from 'node:path'
import {
  type FileLock,
  type LockResult,
  type ReleaseResult,
  type AcquireLockOptions,
  type ReleaseLockOptions,
  type LockStoreConfig,
  type LockEvent,
  type LockEventListener,
  DEFAULT_LOCK_CONFIG,
} from './types.js'

// =============================================================================
// Lock Store
// =============================================================================

/**
 * In-memory file lock store with CAS semantics
 */
export class LockStore {
  private locks: Map<string, FileLock> = new Map()
  private config: Required<LockStoreConfig>
  private cleanupTimer: ReturnType<typeof setInterval> | null = null
  private eventListeners: Set<LockEventListener> = new Set()
  private nextVersion = 1

  constructor(config?: LockStoreConfig) {
    this.config = { ...DEFAULT_LOCK_CONFIG, ...config }

    if (this.config.enableAutoCleanup) {
      this.startCleanup()
    }
  }

  // ===========================================================================
  // Lock Operations
  // ===========================================================================

  /**
   * Acquire a lock on a file
   *
   * Uses CAS semantics - if expectedVersion is provided, the operation
   * only succeeds if the current version matches.
   */
  acquire(filePath: string, options: AcquireLockOptions): LockResult {
    const normalizedPath = this.normalizePath(filePath)
    const now = new Date()
    const ttlMs = options.ttlMs ?? this.config.defaultTtlMs

    // Check for existing lock
    const existingLock = this.locks.get(normalizedPath)

    if (existingLock) {
      // Check if lock has expired
      if (existingLock.expiresAt <= now) {
        // Lock expired, clean it up
        this.releaseLockInternal(normalizedPath, existingLock, 'expired')
      } else if (existingLock.owner.id === options.owner.id) {
        // Same owner - extend the lock (CAS check)
        if (
          options.expectedVersion !== undefined &&
          existingLock.version !== options.expectedVersion
        ) {
          return {
            success: false,
            error: `Version mismatch: expected ${options.expectedVersion}, got ${existingLock.version}`,
            blockedBy: existingLock,
          }
        }

        // Extend the lock
        const newVersion = this.nextVersion++
        const extendedLock: FileLock = {
          ...existingLock,
          expiresAt: new Date(now.getTime() + ttlMs),
          version: newVersion,
          reason: options.reason ?? existingLock.reason,
        }
        this.locks.set(normalizedPath, extendedLock)
        this.emit({
          type: 'extended',
          filePath: normalizedPath,
          owner: options.owner,
          timestamp: now,
        })

        return { success: true, lock: extendedLock }
      } else {
        // Different owner holds the lock
        this.emit({
          type: 'blocked',
          filePath: normalizedPath,
          owner: options.owner,
          previousOwner: existingLock.owner,
          timestamp: now,
        })

        return {
          success: false,
          error: `File is locked by ${existingLock.owner.name} (${existingLock.owner.id})`,
          blockedBy: existingLock,
        }
      }
    }

    // Check max locks per owner
    const ownerLockCount = this.getLocksForOwner(options.owner.id).length
    if (ownerLockCount >= this.config.maxLocksPerOwner) {
      return {
        success: false,
        error: `Owner ${options.owner.id} has reached max locks (${this.config.maxLocksPerOwner})`,
      }
    }

    // Create new lock
    const version = this.nextVersion++
    const lock: FileLock = {
      filePath: normalizedPath,
      owner: options.owner,
      acquiredAt: now,
      expiresAt: new Date(now.getTime() + ttlMs),
      version,
      reason: options.reason,
    }

    this.locks.set(normalizedPath, lock)
    this.emit({
      type: 'acquired',
      filePath: normalizedPath,
      owner: options.owner,
      timestamp: now,
      reason: options.reason,
    })

    return { success: true, lock }
  }

  /**
   * Release a lock on a file
   *
   * Uses CAS semantics for safe concurrent access.
   */
  release(filePath: string, options: ReleaseLockOptions): ReleaseResult {
    const normalizedPath = this.normalizePath(filePath)
    const existingLock = this.locks.get(normalizedPath)

    if (!existingLock) {
      return { success: true } // Already unlocked
    }

    // Check ownership (unless force)
    if (!options.force && existingLock.owner.id !== options.owner.id) {
      return {
        success: false,
        error: `Cannot release lock owned by ${existingLock.owner.name} (${existingLock.owner.id})`,
      }
    }

    // CAS check
    if (options.expectedVersion !== undefined && existingLock.version !== options.expectedVersion) {
      return {
        success: false,
        error: `Version mismatch: expected ${options.expectedVersion}, got ${existingLock.version}`,
      }
    }

    this.releaseLockInternal(normalizedPath, existingLock, 'released')
    return { success: true }
  }

  /**
   * Check if a file is locked
   */
  isLocked(filePath: string): boolean {
    const normalizedPath = this.normalizePath(filePath)
    const lock = this.locks.get(normalizedPath)

    if (!lock) return false

    // Check expiration
    if (lock.expiresAt <= new Date()) {
      this.releaseLockInternal(normalizedPath, lock, 'expired')
      return false
    }

    return true
  }

  /**
   * Get lock info for a file
   */
  getLock(filePath: string): FileLock | null {
    const normalizedPath = this.normalizePath(filePath)
    const lock = this.locks.get(normalizedPath)

    if (!lock) return null

    // Check expiration
    if (lock.expiresAt <= new Date()) {
      this.releaseLockInternal(normalizedPath, lock, 'expired')
      return null
    }

    return lock
  }

  /**
   * Get all locks for an owner
   */
  getLocksForOwner(ownerId: string): FileLock[] {
    const result: FileLock[] = []
    const now = new Date()

    for (const [filePath, lock] of this.locks) {
      if (lock.owner.id === ownerId) {
        if (lock.expiresAt <= now) {
          this.releaseLockInternal(filePath, lock, 'expired')
        } else {
          result.push(lock)
        }
      }
    }

    return result
  }

  /**
   * Get all active locks
   */
  getAllLocks(): FileLock[] {
    const result: FileLock[] = []
    const now = new Date()

    for (const [filePath, lock] of this.locks) {
      if (lock.expiresAt <= now) {
        this.releaseLockInternal(filePath, lock, 'expired')
      } else {
        result.push(lock)
      }
    }

    return result
  }

  /**
   * Release all locks for an owner
   */
  releaseAllForOwner(ownerId: string): number {
    let count = 0

    for (const [filePath, lock] of this.locks) {
      if (lock.owner.id === ownerId) {
        this.releaseLockInternal(filePath, lock, 'released')
        count++
      }
    }

    return count
  }

  // ===========================================================================
  // Batch Operations
  // ===========================================================================

  /**
   * Acquire locks on multiple files atomically
   *
   * Either all locks are acquired or none are.
   */
  acquireMultiple(filePaths: string[], options: AcquireLockOptions): LockResult {
    const acquiredLocks: FileLock[] = []

    // Try to acquire all locks
    for (const filePath of filePaths) {
      const result = this.acquire(filePath, options)

      if (!result.success) {
        // Rollback acquired locks
        for (const lock of acquiredLocks) {
          this.release(lock.filePath, { owner: options.owner, force: true })
        }
        return result
      }

      acquiredLocks.push(result.lock!)
    }

    return {
      success: true,
      lock: acquiredLocks[0], // Return first lock
    }
  }

  /**
   * Release multiple locks
   */
  releaseMultiple(filePaths: string[], options: ReleaseLockOptions): ReleaseResult {
    let lastError: string | undefined

    for (const filePath of filePaths) {
      const result = this.release(filePath, options)
      if (!result.success) {
        lastError = result.error
      }
    }

    if (lastError) {
      return { success: false, error: lastError }
    }

    return { success: true }
  }

  // ===========================================================================
  // Event System
  // ===========================================================================

  /**
   * Add event listener
   */
  on(listener: LockEventListener): void {
    this.eventListeners.add(listener)
  }

  /**
   * Remove event listener
   */
  off(listener: LockEventListener): void {
    this.eventListeners.delete(listener)
  }

  private emit(event: LockEvent): void {
    for (const listener of this.eventListeners) {
      try {
        listener(event)
      } catch {
        // Ignore listener errors
      }
    }
  }

  // ===========================================================================
  // Cleanup
  // ===========================================================================

  private startCleanup(): void {
    if (this.cleanupTimer) return

    this.cleanupTimer = setInterval(() => {
      this.cleanupExpired()
    }, this.config.cleanupIntervalMs)
  }

  /**
   * Clean up expired locks
   */
  cleanupExpired(): number {
    let count = 0
    const now = new Date()

    for (const [filePath, lock] of this.locks) {
      if (lock.expiresAt <= now) {
        this.releaseLockInternal(filePath, lock, 'expired')
        count++
      }
    }

    return count
  }

  private releaseLockInternal(
    filePath: string,
    lock: FileLock,
    reason: 'released' | 'expired'
  ): void {
    this.locks.delete(filePath)
    this.emit({ type: reason, filePath, owner: lock.owner, timestamp: new Date() })
  }

  /**
   * Stop cleanup timer and clear all locks
   */
  destroy(): void {
    if (this.cleanupTimer) {
      clearInterval(this.cleanupTimer)
      this.cleanupTimer = null
    }
    this.locks.clear()
    this.eventListeners.clear()
  }

  // ===========================================================================
  // Utilities
  // ===========================================================================

  private normalizePath(filePath: string): string {
    return path.resolve(filePath)
  }

  /**
   * Get statistics about the lock store
   */
  getStats(): {
    totalLocks: number
    ownerCounts: Map<string, number>
    oldestLock: FileLock | null
    newestLock: FileLock | null
  } {
    const locks = this.getAllLocks()
    const ownerCounts = new Map<string, number>()

    let oldestLock: FileLock | null = null
    let newestLock: FileLock | null = null

    for (const lock of locks) {
      const count = ownerCounts.get(lock.owner.id) ?? 0
      ownerCounts.set(lock.owner.id, count + 1)

      if (!oldestLock || lock.acquiredAt < oldestLock.acquiredAt) {
        oldestLock = lock
      }
      if (!newestLock || lock.acquiredAt > newestLock.acquiredAt) {
        newestLock = lock
      }
    }

    return {
      totalLocks: locks.length,
      ownerCounts,
      oldestLock,
      newestLock,
    }
  }
}

// =============================================================================
// Singleton Instance
// =============================================================================

let globalLockStore: LockStore | null = null

/**
 * Get the global lock store instance
 */
export function getLockStore(config?: LockStoreConfig): LockStore {
  if (!globalLockStore) {
    globalLockStore = new LockStore(config)
  }
  return globalLockStore
}

/**
 * Reset the global lock store (for testing)
 */
export function resetLockStore(): void {
  if (globalLockStore) {
    globalLockStore.destroy()
    globalLockStore = null
  }
}
