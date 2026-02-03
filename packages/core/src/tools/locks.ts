/**
 * File Locking System
 * Prevents concurrent edits to the same file
 *
 * Uses a promise-chain pattern to serialize operations on the same file path.
 * This is an in-memory lock suitable for single-process scenarios.
 */

// ============================================================================
// Types
// ============================================================================

/** Lock entry with metadata */
interface LockEntry {
  /** The promise that represents this lock */
  promise: Promise<void>
  /** Function to release the lock */
  release: () => void
  /** When the lock was acquired */
  acquiredAt: number
  /** Description of the operation holding the lock */
  operation?: string
}

/** Lock state for debugging/monitoring */
export interface LockInfo {
  path: string
  acquiredAt: number
  operation?: string
  waitingCount: number
}

// ============================================================================
// Lock Storage
// ============================================================================

/** Active file locks by path */
const fileLocks = new Map<string, LockEntry>()

/** Waiting operations by path */
const waitingCounts = new Map<string, number>()

// ============================================================================
// Lock Functions
// ============================================================================

/**
 * Execute a function while holding an exclusive lock on a file path
 *
 * @param path - The file path to lock
 * @param fn - The async function to execute with the lock
 * @param operation - Optional description of the operation (for debugging)
 * @returns The result of the function
 *
 * @example
 * ```typescript
 * await withFileLock('/path/to/file.ts', async () => {
 *   const content = await fs.readFile(path)
 *   const modified = transform(content)
 *   await fs.writeFile(path, modified)
 * }, 'write operation')
 * ```
 */
export async function withFileLock<T>(
  path: string,
  fn: () => Promise<T>,
  operation?: string
): Promise<T> {
  // Normalize path for consistent locking
  const normalizedPath = normalizePath(path)

  // Get the current lock (if any)
  const currentLock = fileLocks.get(normalizedPath)

  // Track waiting count
  const currentWaiting = waitingCounts.get(normalizedPath) ?? 0
  waitingCounts.set(normalizedPath, currentWaiting + 1)

  // Create a new lock that waits for the previous one
  let releaseFn: () => void
  const lockPromise = new Promise<void>((resolve) => {
    releaseFn = resolve
  })

  const newLock: LockEntry = {
    promise: lockPromise,
    release: releaseFn!,
    acquiredAt: Date.now(),
    operation,
  }

  // Set our lock before waiting (claim our spot in line)
  fileLocks.set(normalizedPath, newLock)

  try {
    // Wait for any existing lock to release
    if (currentLock) {
      await currentLock.promise
    }

    // Decrement waiting count (we now have the lock)
    const waiting = waitingCounts.get(normalizedPath) ?? 1
    if (waiting <= 1) {
      waitingCounts.delete(normalizedPath)
    } else {
      waitingCounts.set(normalizedPath, waiting - 1)
    }

    // Execute the function with the lock held
    return await fn()
  } finally {
    // Release our lock
    newLock.release()

    // Clean up if this is still our lock
    if (fileLocks.get(normalizedPath) === newLock) {
      fileLocks.delete(normalizedPath)
    }
  }
}

/**
 * Try to acquire a lock without waiting
 *
 * @param path - The file path to lock
 * @param fn - The async function to execute with the lock
 * @param operation - Optional description of the operation
 * @returns The result of the function, or null if the lock couldn't be acquired
 */
export async function tryFileLock<T>(
  path: string,
  fn: () => Promise<T>,
  operation?: string
): Promise<T | null> {
  const normalizedPath = normalizePath(path)

  // If there's an existing lock, fail immediately
  if (fileLocks.has(normalizedPath)) {
    return null
  }

  // No existing lock, proceed with withFileLock
  return withFileLock(normalizedPath, fn, operation)
}

/**
 * Check if a file is currently locked
 *
 * @param path - The file path to check
 * @returns True if the file is locked
 */
export function isFileLocked(path: string): boolean {
  return fileLocks.has(normalizePath(path))
}

/**
 * Get information about all active locks
 *
 * @returns Array of lock information
 */
export function getActiveLocks(): LockInfo[] {
  const locks: LockInfo[] = []

  for (const [path, lock] of fileLocks) {
    locks.push({
      path,
      acquiredAt: lock.acquiredAt,
      operation: lock.operation,
      waitingCount: waitingCounts.get(path) ?? 0,
    })
  }

  return locks
}

/**
 * Get information about a specific lock
 *
 * @param path - The file path to check
 * @returns Lock information, or null if not locked
 */
export function getLockInfo(path: string): LockInfo | null {
  const normalizedPath = normalizePath(path)
  const lock = fileLocks.get(normalizedPath)

  if (!lock) {
    return null
  }

  return {
    path: normalizedPath,
    acquiredAt: lock.acquiredAt,
    operation: lock.operation,
    waitingCount: waitingCounts.get(normalizedPath) ?? 0,
  }
}

/**
 * Clear all locks (for testing/cleanup)
 * WARNING: This can cause race conditions if operations are in progress
 */
export function clearAllLocks(): void {
  // Release all locks
  for (const lock of fileLocks.values()) {
    lock.release()
  }
  fileLocks.clear()
  waitingCounts.clear()
}

// ============================================================================
// Utilities
// ============================================================================

/**
 * Normalize a file path for consistent locking
 */
function normalizePath(path: string): string {
  // Remove trailing slashes and normalize separators
  return path.replace(/\\/g, '/').replace(/\/+$/, '')
}
