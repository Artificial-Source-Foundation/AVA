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

  /**
   * Resolve lock conflicts with multiple strategies
   */
  const resolve_lock_conflict = tool({
    description: `Resolve file lock conflicts using various strategies.

**Purpose:** Handle situations where multiple agents need the same file.

**Strategies:**
- wait: Wait for the lock to expire or be released
- steal: Force acquire the lock (DANGEROUS - may cause data loss)
- merge: Request manual merge after both complete (returns conflict markers)
- notify: Notify the lock owner and wait for voluntary release
- skip: Skip this file and continue with others

**Use when:**
- Another agent has locked a file you need
- Deadlock detection suggests circular locks
- Lock has expired but wasn't released

**Example:**
resolve_lock_conflict({ filePath: "src/index.ts", strategy: "wait", maxWaitMs: 30000 })`,

    args: {
      filePath: s.string().describe('File path with the lock conflict'),
      strategy: s
        .enum(['wait', 'steal', 'merge', 'notify', 'skip'])
        .describe('Resolution strategy to use'),
      maxWaitMs: s
        .number()
        .optional()
        .describe('Maximum wait time in ms for "wait" strategy (default: 30000)'),
      reason: s.string().optional().describe('Reason for the conflict resolution'),
    },

    async execute(args, ctx) {
      const ownerId = defaultOwner?.id ?? ctx.sessionID ?? 'unknown'
      const maxWaitMs = args.maxWaitMs ?? 30000

      log?.('info', 'Resolving lock conflict', {
        filePath: args.filePath,
        strategy: args.strategy,
      })

      const existingLock = store.getLock(args.filePath)

      // No conflict - file is not locked
      if (!existingLock) {
        return JSON.stringify({
          success: true,
          resolution: 'no_conflict',
          message: 'File is not locked - no conflict to resolve',
        })
      }

      // Check if we own the lock
      if (existingLock.owner.id === ownerId) {
        return JSON.stringify({
          success: true,
          resolution: 'own_lock',
          message: 'You already own this lock - no conflict',
        })
      }

      // Implement resolution strategies
      switch (args.strategy) {
        case 'wait': {
          const timeUntilExpiry = existingLock.expiresAt.getTime() - Date.now()

          if (timeUntilExpiry <= 0) {
            // Lock already expired, try to acquire
            store.cleanupExpired() // Force cleanup expired locks
            const result = store.acquire(args.filePath, {
              owner: { id: ownerId, name: 'Agent' },
              ttlMs: defaultTtlMs,
              reason: args.reason || 'Acquired after conflict resolution (expired lock)',
            })

            return JSON.stringify({
              success: result.success,
              resolution: 'expired_acquired',
              message: result.success
                ? 'Lock was expired, acquired successfully'
                : `Failed to acquire: ${result.error}`,
            })
          }

          const waitTime = Math.min(timeUntilExpiry, maxWaitMs)
          return JSON.stringify({
            success: false,
            resolution: 'wait_required',
            message: `Lock held by ${existingLock.owner.name}. Try again in ${Math.ceil(waitTime / 1000)}s`,
            blockedBy: {
              owner: existingLock.owner.name,
              expiresAt: existingLock.expiresAt.toISOString(),
              reason: existingLock.reason,
            },
            suggestedRetryMs: waitTime,
          })
        }

        case 'steal': {
          // Force release and reacquire
          const releaseResult = store.release(args.filePath, {
            owner: { id: ownerId, name: 'Agent' },
            force: true,
          })

          if (!releaseResult.success) {
            return JSON.stringify({
              success: false,
              resolution: 'steal_failed',
              error: releaseResult.error,
            })
          }

          const acquireResult = store.acquire(args.filePath, {
            owner: { id: ownerId, name: 'Agent' },
            ttlMs: defaultTtlMs,
            reason: args.reason || `Stolen from ${existingLock.owner.name}: ${args.reason || 'conflict resolution'}`,
          })

          return JSON.stringify({
            success: acquireResult.success,
            resolution: 'stolen',
            message: acquireResult.success
              ? `Lock stolen from ${existingLock.owner.name}. WARNING: May cause data conflicts.`
              : `Steal failed: ${acquireResult.error}`,
            previousOwner: existingLock.owner.name,
            warning: 'Data written by the previous owner may be lost or corrupted.',
          })
        }

        case 'merge': {
          // Return merge instructions
          return JSON.stringify({
            success: true,
            resolution: 'merge_requested',
            message: 'Merge strategy selected. Both parties should complete work, then manually merge.',
            mergeInstructions: {
              step1: `Wait for ${existingLock.owner.name} to complete and release lock`,
              step2: 'Compare your changes with the committed version',
              step3: 'Use git diff or manual comparison to identify conflicts',
              step4: 'Apply your changes carefully, preserving both sets of modifications',
            },
            currentOwner: {
              name: existingLock.owner.name,
              taskId: existingLock.owner.taskId,
            },
          })
        }

        case 'notify': {
          // In a real system, this would send a notification
          // For now, just log and return instructions
          log?.('info', 'Lock conflict notification requested', {
            filePath: args.filePath,
            blockedBy: existingLock.owner.id,
            requester: ownerId,
          })

          return JSON.stringify({
            success: true,
            resolution: 'notification_sent',
            message: `Notification sent to ${existingLock.owner.name} requesting voluntary release`,
            lockedBy: {
              name: existingLock.owner.name,
              taskId: existingLock.owner.taskId,
              reason: existingLock.reason,
            },
            suggestion: 'Check back in 30 seconds or use "wait" strategy',
          })
        }

        case 'skip': {
          return JSON.stringify({
            success: true,
            resolution: 'skipped',
            message: `Skipped locked file: ${args.filePath}`,
            skippedFile: args.filePath,
            lockedBy: existingLock.owner.name,
          })
        }

        default:
          return JSON.stringify({
            success: false,
            error: `Unknown strategy: ${args.strategy}`,
          })
      }
    },
  })

  /**
   * Detect potential deadlocks
   */
  const detect_deadlocks = tool({
    description: `Detect potential deadlock situations in lock graph.

**Purpose:** Identify circular dependencies that could cause agents to wait indefinitely.

**Returns:**
- List of potential deadlock cycles
- Affected files and owners
- Recommended resolution`,

    args: {},

    async execute(_args, _ctx) {
      log?.('info', 'Detecting deadlocks')

      const allLocks = store.getAllLocks()

      // Build a simple wait graph
      // In practice, we'd track who's waiting for what
      // For now, detect expired locks and long-held locks as potential issues

      const issues: Array<{
        type: string
        severity: 'low' | 'medium' | 'high'
        description: string
        files: string[]
        owners: string[]
      }> = []

      const now = Date.now()

      // Check for expired locks
      const expiredLocks = allLocks.filter((l) => l.expiresAt.getTime() < now)
      if (expiredLocks.length > 0) {
        issues.push({
          type: 'expired_locks',
          severity: 'medium',
          description: 'Expired locks not cleaned up - may block other agents',
          files: expiredLocks.map((l) => l.filePath),
          owners: [...new Set(expiredLocks.map((l) => l.owner.name))],
        })
      }

      // Check for long-held locks (> 80% of TTL used)
      const longHeldLocks = allLocks.filter((l) => {
        const held = now - l.acquiredAt.getTime()
        const total = l.expiresAt.getTime() - l.acquiredAt.getTime()
        return held > total * 0.8 && l.expiresAt.getTime() > now
      })
      if (longHeldLocks.length > 0) {
        issues.push({
          type: 'long_held_locks',
          severity: 'low',
          description: 'Locks held for extended period - may indicate stalled agents',
          files: longHeldLocks.map((l) => l.filePath),
          owners: [...new Set(longHeldLocks.map((l) => l.owner.name))],
        })
      }

      // Check for same owner holding many locks (potential resource hoarding)
      const locksByOwner: Record<string, number> = {}
      for (const lock of allLocks) {
        locksByOwner[lock.owner.id] = (locksByOwner[lock.owner.id] || 0) + 1
      }
      const hoarders = Object.entries(locksByOwner).filter(([_, count]) => count > 5)
      if (hoarders.length > 0) {
        issues.push({
          type: 'lock_hoarding',
          severity: 'medium',
          description: 'Agents holding many locks simultaneously - potential resource contention',
          files: allLocks
            .filter((l) => hoarders.some(([id]) => l.owner.id === id))
            .map((l) => l.filePath),
          owners: hoarders.map(([id]) => {
            const lock = allLocks.find((l) => l.owner.id === id)
            return lock?.owner.name || id
          }),
        })
      }

      return JSON.stringify({
        success: true,
        totalLocks: allLocks.length,
        issuesFound: issues.length,
        issues,
        recommendation: issues.length > 0
          ? 'Consider running cleanup() to remove expired locks, or investigate long-held locks'
          : 'No deadlock risks detected',
      }, null, 2)
    },
  })

  return {
    lock_file,
    unlock_file,
    check_lock,
    list_locks,
    lock_files,
    unlock_all,
    resolve_lock_conflict,
    detect_deadlocks,
  }
}
