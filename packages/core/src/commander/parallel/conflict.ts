/**
 * File Conflict Detection
 * Detects and prevents concurrent writes to the same file
 *
 * Rules:
 * - read + read = OK (multiple readers)
 * - read + write = CONFLICT
 * - write + write = CONFLICT
 */

import type { BatchTask, ConflictInfo, ConflictResult, FileAccess } from '../types.js'

// ============================================================================
// Conflict Detector
// ============================================================================

/**
 * Detects file access conflicts before parallel execution
 *
 * Tracks file access claims from workers and detects conflicts
 * using reader-writer semantics (multiple readers OR single writer)
 */
export class ConflictDetector {
  /** Map of file path → list of access claims */
  private claims: Map<string, FileAccess[]> = new Map()

  /**
   * Declare intended file access before execution
   *
   * @param access - File access declaration
   * @returns Conflict result
   */
  declareAccess(access: FileAccess): ConflictResult {
    const existing = this.claims.get(access.path) ?? []

    // Check for conflicts
    for (const claim of existing) {
      // Same worker can access same file multiple times
      if (claim.workerId === access.workerId) continue

      // Multiple readers are OK
      if (claim.mode === 'read' && access.mode === 'read') continue

      // Any write involved = conflict
      return {
        conflict: true,
        blockedBy: claim.workerId,
        path: access.path,
      }
    }

    // No conflict - add claim
    existing.push(access)
    this.claims.set(access.path, existing)

    return { conflict: false }
  }

  /**
   * Check all access for a worker
   *
   * @param workerId - Worker ID
   * @param accesses - List of file accesses
   * @returns Array of conflict results
   */
  checkWorker(workerId: string, accesses: FileAccess[]): ConflictResult[] {
    return accesses.map((access) => this.declareAccess({ ...access, workerId }))
  }

  /**
   * Release all claims for a worker
   *
   * @param workerId - Worker ID
   */
  release(workerId: string): void {
    for (const [path, claims] of this.claims.entries()) {
      const filtered = claims.filter((c) => c.workerId !== workerId)
      if (filtered.length === 0) {
        this.claims.delete(path)
      } else {
        this.claims.set(path, filtered)
      }
    }
  }

  /**
   * Clear all claims
   */
  clear(): void {
    this.claims.clear()
  }

  /**
   * Get all current claims
   */
  getClaims(): Map<string, FileAccess[]> {
    return new Map(this.claims)
  }

  /**
   * Check if a path has any claims
   */
  hasClaims(path: string): boolean {
    return this.claims.has(path)
  }

  /**
   * Check if a path has write claims
   */
  hasWriteClaims(path: string): boolean {
    const claims = this.claims.get(path)
    return claims?.some((c) => c.mode === 'write') ?? false
  }
}

// ============================================================================
// Task Partitioning
// ============================================================================

/**
 * Result of partitioning tasks for parallel execution
 */
export interface PartitionResult {
  /** Tasks that can run in parallel (no conflicts) */
  parallel: BatchTask[]
  /** Tasks that must run sequentially (have conflicts) */
  serialized: BatchTask[]
  /** Conflicts detected */
  conflicts: ConflictInfo[]
}

/**
 * Partition tasks into parallel and serialized groups based on file conflicts
 *
 * Tasks with expectedPaths are checked for conflicts.
 * Tasks without expectedPaths are assumed safe for parallel execution.
 *
 * @param tasks - Tasks to partition
 * @returns Partition result
 */
export function partitionTasks(tasks: BatchTask[]): PartitionResult {
  const detector = new ConflictDetector()
  const parallel: BatchTask[] = []
  const serialized: BatchTask[] = []
  const conflicts: ConflictInfo[] = []

  // Track which workers conflict with which
  const conflictMap: Map<string, Set<string>> = new Map()

  for (const task of tasks) {
    const paths = getExpectedPaths(task)

    if (paths.length === 0) {
      // No declared paths - assume parallel safe
      parallel.push(task)
      continue
    }

    // Check each path for conflicts
    const taskConflicts: ConflictResult[] = []
    for (const path of paths) {
      const result = detector.declareAccess({
        path: path.path,
        mode: path.mode,
        workerId: task.id,
      })
      taskConflicts.push(result)
    }

    // Check if any conflicts
    const hasConflict = taskConflicts.some((c) => c.conflict)

    if (hasConflict) {
      serialized.push(task)

      // Track conflict info
      for (const result of taskConflicts) {
        if (result.conflict) {
          const existing = conflictMap.get(result.path) ?? new Set()
          existing.add(task.id)
          existing.add(result.blockedBy)
          conflictMap.set(result.path, existing)
        }
      }
    } else {
      parallel.push(task)
    }
  }

  // Convert conflict map to ConflictInfo array
  for (const [path, workers] of conflictMap) {
    conflicts.push({
      path,
      workers: Array.from(workers),
      resolution: 'serialized',
    })
  }

  return { parallel, serialized, conflicts }
}

/**
 * Get expected file accesses from a task
 */
function getExpectedPaths(task: BatchTask): Array<{ path: string; mode: 'read' | 'write' }> {
  const paths: Array<{ path: string; mode: 'read' | 'write' }> = []

  // Check for DependentTask with expectedPaths
  const scheduled = task as { expectedPaths?: { reads?: string[]; writes?: string[] } }

  if (scheduled.expectedPaths) {
    for (const path of scheduled.expectedPaths.reads ?? []) {
      paths.push({ path, mode: 'read' })
    }
    for (const path of scheduled.expectedPaths.writes ?? []) {
      paths.push({ path, mode: 'write' })
    }
  }

  return paths
}

// ============================================================================
// Conflict-Aware Execution
// ============================================================================

/**
 * Execute a batch with conflict awareness
 *
 * 1. Partitions tasks based on file conflicts
 * 2. Executes non-conflicting tasks in parallel
 * 3. Executes conflicting tasks sequentially
 *
 * @param tasks - Tasks to execute
 * @param executeBatch - Function to execute a batch
 * @param executeSequential - Function to execute sequentially
 * @returns Combined results with conflict info
 */
export async function executeWithConflictDetection<T>(
  tasks: BatchTask[],
  executeBatch: (tasks: BatchTask[]) => Promise<T>,
  executeSequential: (tasks: BatchTask[]) => Promise<T>,
  combineResults: (parallel: T, serial: T, conflicts: ConflictInfo[]) => T
): Promise<{ result: T; conflicts: ConflictInfo[] }> {
  const { parallel, serialized, conflicts } = partitionTasks(tasks)

  // Execute non-conflicting in parallel
  const parallelResult = parallel.length > 0 ? await executeBatch(parallel) : null

  // Execute conflicting sequentially
  const serialResult = serialized.length > 0 ? await executeSequential(serialized) : null

  // Combine results
  if (parallelResult && serialResult) {
    return {
      result: combineResults(parallelResult, serialResult, conflicts),
      conflicts,
    }
  }

  return {
    result: (parallelResult ?? serialResult) as T,
    conflicts,
  }
}
