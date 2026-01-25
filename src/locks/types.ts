/**
 * Delta9 File Reservation System - Type Definitions
 *
 * CAS-based file locks to prevent parallel edit conflicts.
 * Locks are held during operator work and released on completion/timeout.
 */

import { z } from 'zod'

// =============================================================================
// Lock Types
// =============================================================================

/** Lock owner identification */
export interface LockOwner {
  /** Agent or session ID */
  id: string
  /** Human-readable name */
  name: string
  /** Task being performed */
  taskId?: string
}

/** File lock state */
export interface FileLock {
  /** Absolute file path */
  filePath: string
  /** Lock owner */
  owner: LockOwner
  /** When the lock was acquired */
  acquiredAt: Date
  /** When the lock expires (TTL-based) */
  expiresAt: Date
  /** Lock version for CAS operations */
  version: number
  /** Optional reason for the lock */
  reason?: string
}

/** Lock acquisition result */
export interface LockResult {
  /** Whether lock was acquired */
  success: boolean
  /** The lock if acquired */
  lock?: FileLock
  /** Error message if failed */
  error?: string
  /** Existing lock holder if blocked */
  blockedBy?: FileLock
}

/** Lock release result */
export interface ReleaseResult {
  /** Whether release was successful */
  success: boolean
  /** Error message if failed */
  error?: string
}

// =============================================================================
// Lock Options
// =============================================================================

/** Options for acquiring a lock */
export interface AcquireLockOptions {
  /** Lock owner */
  owner: LockOwner
  /** Time-to-live in milliseconds (default: 5 minutes) */
  ttlMs?: number
  /** Reason for the lock */
  reason?: string
  /** Wait for lock if blocked (default: false) */
  wait?: boolean
  /** Maximum wait time in milliseconds */
  waitTimeoutMs?: number
  /** Expected version for CAS (if extending existing lock) */
  expectedVersion?: number
}

/** Options for releasing a lock */
export interface ReleaseLockOptions {
  /** Lock owner (must match) */
  owner: LockOwner
  /** Expected version for CAS */
  expectedVersion?: number
  /** Force release even if not owner */
  force?: boolean
}

// =============================================================================
// Store Configuration
// =============================================================================

/** Lock store configuration */
export interface LockStoreConfig {
  /** Default TTL in milliseconds (default: 5 minutes) */
  defaultTtlMs?: number
  /** Cleanup interval in milliseconds (default: 1 minute) */
  cleanupIntervalMs?: number
  /** Maximum locks per owner (default: 10) */
  maxLocksPerOwner?: number
  /** Enable stale lock detection and auto-cleanup */
  enableAutoCleanup?: boolean
}

/** Default configuration values */
export const DEFAULT_LOCK_CONFIG: Required<LockStoreConfig> = {
  defaultTtlMs: 5 * 60 * 1000, // 5 minutes
  cleanupIntervalMs: 60 * 1000, // 1 minute
  maxLocksPerOwner: 10,
  enableAutoCleanup: true,
}

// =============================================================================
// Schemas
// =============================================================================

/** Lock owner schema */
export const LockOwnerSchema = z.object({
  id: z.string().min(1),
  name: z.string().min(1),
  taskId: z.string().optional(),
})

/** Acquire lock options schema */
export const AcquireLockOptionsSchema = z.object({
  owner: LockOwnerSchema,
  ttlMs: z.number().positive().optional(),
  reason: z.string().optional(),
  wait: z.boolean().optional(),
  waitTimeoutMs: z.number().positive().optional(),
  expectedVersion: z.number().int().nonnegative().optional(),
})

/** Release lock options schema */
export const ReleaseLockOptionsSchema = z.object({
  owner: LockOwnerSchema,
  expectedVersion: z.number().int().nonnegative().optional(),
  force: z.boolean().optional(),
})

// =============================================================================
// Event Types
// =============================================================================

/** Lock event types */
export type LockEventType = 'acquired' | 'released' | 'expired' | 'extended' | 'blocked'

/** Lock event */
export interface LockEvent {
  type: LockEventType
  filePath: string
  owner: LockOwner
  timestamp: Date
  previousOwner?: LockOwner
  reason?: string
}

/** Lock event listener */
export type LockEventListener = (event: LockEvent) => void
