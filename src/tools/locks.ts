/**
 * Delta9 Lock Tools
 *
 * Tools for file reservation system:
 * - lock_file: Acquire a lock on one or more files
 * - unlock_file: Release a lock on one or more files
 * - check_lock: Check if a file is locked
 * - list_locks: List all active locks
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import { getLockStore, type LockOwner } from '../locks/index.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Tool Factory
// =============================================================================

export interface LockToolsConfig {
  /** Default owner for locks (if not specified in tool args) */
  defaultOwner?: LockOwner
  /** Default TTL in milliseconds */
  defaultTtlMs?: number
  /** Logger function */
  log?: (level: string, message: string, data?: Record<string, unknown>) => void
}

/**
 * Create lock tools with bound context
 */
export function createLockTools(config: LockToolsConfig = {}): Record<string, ToolDefinition> {
  const { defaultOwner, defaultTtlMs = 5 * 60 * 1000, log } = config
  const store = getLockStore()

  /**
   * Acquire a lock on one or more files
   */
  const lock_file = tool({
    description:
      'Acquire an exclusive lock on a file to prevent concurrent edits. The lock expires after TTL (default 5 minutes).',
    args: {
      filePath: s.string().describe('File path to lock'),
      ownerId: s.string().optional().describe('Lock owner ID (defaults to session ID)'),
      ownerName: s.string().optional().describe('Lock owner name'),
      taskId: s.string().optional().describe('Task ID associated with this lock'),
      ttlMs: s
        .number()
        .optional()
        .describe('Lock TTL in milliseconds (default: 300000 = 5 minutes)'),
      reason: s.string().optional().describe('Reason for the lock'),
    },

    async execute(args, ctx) {
      const owner: LockOwner = {
        id: args.ownerId ?? defaultOwner?.id ?? ctx.sessionID ?? 'unknown',
        name: args.ownerName ?? defaultOwner?.name ?? 'Agent',
        taskId: args.taskId,
      }

      log?.('info', 'Acquiring file lock', { filePath: args.filePath, owner: owner.id })

      const result = store.acquire(args.filePath, {
        owner,
        ttlMs: args.ttlMs ?? defaultTtlMs,
        reason: args.reason,
      })

      if (result.success) {
        return JSON.stringify({
          success: true,
          message: `Lock acquired on ${args.filePath}`,
          lock: {
            filePath: result.lock!.filePath,
            owner: result.lock!.owner.name,
            version: result.lock!.version,
            expiresAt: result.lock!.expiresAt.toISOString(),
          },
        })
      } else {
        return JSON.stringify({
          success: false,
          error: result.error,
          blockedBy: result.blockedBy
            ? {
                owner: result.blockedBy.owner.name,
                ownerId: result.blockedBy.owner.id,
                acquiredAt: result.blockedBy.acquiredAt.toISOString(),
                expiresAt: result.blockedBy.expiresAt.toISOString(),
                reason: result.blockedBy.reason,
              }
            : undefined,
        })
      }
    },
  })

  /**
   * Release a lock on a file
   */
  const unlock_file = tool({
    description:
      'Release an exclusive lock on a file. You must own the lock to release it (unless force is true).',
    args: {
      filePath: s.string().describe('File path to unlock'),
      ownerId: s.string().optional().describe('Lock owner ID (must match the lock owner)'),
      force: s.boolean().optional().describe('Force release even if not the owner'),
    },

    async execute(args, ctx) {
      const ownerId = args.ownerId ?? defaultOwner?.id ?? ctx.sessionID ?? 'unknown'

      log?.('info', 'Releasing file lock', { filePath: args.filePath, owner: ownerId })

      const result = store.release(args.filePath, {
        owner: { id: ownerId, name: 'Agent' },
        force: args.force,
      })

      if (result.success) {
        return JSON.stringify({
          success: true,
          message: `Lock released on ${args.filePath}`,
        })
      } else {
        return JSON.stringify({
          success: false,
          error: result.error,
        })
      }
    },
  })

  /**
   * Check if a file is locked
   */
  const check_lock = tool({
    description: 'Check if a file is currently locked and get lock information.',
    args: {
      filePath: s.string().describe('File path to check'),
    },

    async execute(args, _ctx) {
      log?.('debug', 'Checking file lock', { filePath: args.filePath })

      const lock = store.getLock(args.filePath)

      if (lock) {
        return JSON.stringify({
          isLocked: true,
          lock: {
            filePath: lock.filePath,
            owner: lock.owner.name,
            ownerId: lock.owner.id,
            taskId: lock.owner.taskId,
            acquiredAt: lock.acquiredAt.toISOString(),
            expiresAt: lock.expiresAt.toISOString(),
            version: lock.version,
            reason: lock.reason,
          },
        })
      } else {
        return JSON.stringify({
          isLocked: false,
        })
      }
    },
  })

  /**
   * List all active locks
   */
  const list_locks = tool({
    description: 'List all active file locks, optionally filtered by owner.',
    args: {
      ownerId: s.string().optional().describe('Filter locks by owner ID'),
    },

    async execute(args, _ctx) {
      log?.('debug', 'Listing file locks', { ownerId: args.ownerId })

      const locks = args.ownerId ? store.getLocksForOwner(args.ownerId) : store.getAllLocks()

      return JSON.stringify({
        count: locks.length,
        locks: locks.map((lock) => ({
          filePath: lock.filePath,
          owner: lock.owner.name,
          ownerId: lock.owner.id,
          taskId: lock.owner.taskId,
          acquiredAt: lock.acquiredAt.toISOString(),
          expiresAt: lock.expiresAt.toISOString(),
          version: lock.version,
          reason: lock.reason,
        })),
      })
    },
  })

  /**
   * Lock multiple files atomically
   */
  const lock_files = tool({
    description:
      'Acquire exclusive locks on multiple files atomically. Either all locks succeed or none do.',
    args: {
      filePaths: s.string().describe('Comma-separated list of file paths to lock'),
      ownerId: s.string().optional().describe('Lock owner ID'),
      ownerName: s.string().optional().describe('Lock owner name'),
      taskId: s.string().optional().describe('Task ID'),
      ttlMs: s.number().optional().describe('Lock TTL in milliseconds'),
      reason: s.string().optional().describe('Reason for the locks'),
    },

    async execute(args, ctx) {
      const owner: LockOwner = {
        id: args.ownerId ?? defaultOwner?.id ?? ctx.sessionID ?? 'unknown',
        name: args.ownerName ?? defaultOwner?.name ?? 'Agent',
        taskId: args.taskId,
      }

      const filePaths = args.filePaths.split(',').map((p) => p.trim())

      log?.('info', 'Acquiring multiple file locks', { count: filePaths.length, owner: owner.id })

      const result = store.acquireMultiple(filePaths, {
        owner,
        ttlMs: args.ttlMs ?? defaultTtlMs,
        reason: args.reason,
      })

      if (result.success) {
        const locks = filePaths.map((fp) => store.getLock(fp)).filter(Boolean)
        return JSON.stringify({
          success: true,
          message: `Acquired ${locks.length} locks`,
          lockedFiles: locks.map((l) => l!.filePath),
        })
      } else {
        return JSON.stringify({
          success: false,
          error: result.error,
          blockedBy: result.blockedBy
            ? {
                filePath: result.blockedBy.filePath,
                owner: result.blockedBy.owner.name,
              }
            : undefined,
        })
      }
    },
  })

  /**
   * Release all locks for an owner
   */
  const unlock_all = tool({
    description: 'Release all locks held by a specific owner.',
    args: {
      ownerId: s
        .string()
        .optional()
        .describe('Owner ID to release locks for (defaults to current session)'),
    },

    async execute(args, ctx) {
      const ownerId = args.ownerId ?? defaultOwner?.id ?? ctx.sessionID ?? 'unknown'

      log?.('info', 'Releasing all locks for owner', { owner: ownerId })

      const count = store.releaseAllForOwner(ownerId)

      return JSON.stringify({
        success: true,
        message: `Released ${count} locks`,
        count,
      })
    },
  })

  return {
    lock_file,
    unlock_file,
    check_lock,
    list_locks,
    lock_files,
    unlock_all,
  }
}
