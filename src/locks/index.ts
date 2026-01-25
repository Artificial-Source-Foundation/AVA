/**
 * Delta9 File Reservation System
 *
 * CAS-based file locks to prevent parallel edit conflicts.
 *
 * Features:
 * - Atomic lock/unlock operations with CAS semantics
 * - TTL-based automatic expiration (default 5 minutes)
 * - Version tracking for safe concurrent access
 * - Event emission for monitoring
 * - Batch operations for multi-file locking
 */

// Types
export type {
  LockOwner,
  FileLock,
  LockResult,
  ReleaseResult,
  AcquireLockOptions,
  ReleaseLockOptions,
  LockStoreConfig,
  LockEvent,
  LockEventType,
  LockEventListener,
} from './types.js'

export { LockOwnerSchema, AcquireLockOptionsSchema, ReleaseLockOptionsSchema, DEFAULT_LOCK_CONFIG } from './types.js'

// Store
export { LockStore, getLockStore, resetLockStore } from './store.js'
