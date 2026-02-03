/**
 * Diff Tracker
 * Track pending file edits with unified diffs
 */

import type {
  CreateEditOptions,
  DiffTrackerEvent,
  DiffTrackerListener,
  EditStatus,
  PendingEdit,
} from './types.js'
import { createDiff, getDiffStats, hasChanges } from './unified.js'

// ============================================================================
// Diff Tracker Class
// ============================================================================

/**
 * Tracks pending file edits with unified diffs
 *
 * @example
 * ```typescript
 * const tracker = new DiffTracker()
 *
 * // Add an edit
 * const edit = tracker.add('src/main.ts', originalContent, modifiedContent)
 * console.log(edit.diff) // Unified diff
 *
 * // List pending edits
 * const pending = tracker.getPending()
 *
 * // Apply or reject
 * tracker.apply(edit.id)
 * // or
 * tracker.reject(edit.id)
 * ```
 */
export class DiffTracker {
  private edits = new Map<string, PendingEdit>()
  private editsByPath = new Map<string, Set<string>>()
  private listeners = new Set<DiffTrackerListener>()

  // ==========================================================================
  // Core Operations
  // ==========================================================================

  /**
   * Add a pending edit
   *
   * @param path - File path
   * @param original - Original content
   * @param modified - Modified content
   * @param options - Optional settings
   * @returns The created PendingEdit
   */
  add(path: string, original: string, modified: string, options?: CreateEditOptions): PendingEdit {
    const diff = createDiff(path, original, modified)

    // Skip if no actual changes
    if (!hasChanges(diff)) {
      const edit: PendingEdit = {
        id: options?.id ?? this.generateId(),
        path,
        original,
        modified,
        diff: '',
        status: 'applied', // No changes = already applied
        createdAt: Date.now(),
        resolvedAt: Date.now(),
        description: options?.description,
      }
      return edit
    }

    const edit: PendingEdit = {
      id: options?.id ?? this.generateId(),
      path,
      original,
      modified,
      diff,
      status: 'pending',
      createdAt: Date.now(),
      description: options?.description,
    }

    this.edits.set(edit.id, edit)

    // Track by path
    if (!this.editsByPath.has(path)) {
      this.editsByPath.set(path, new Set())
    }
    this.editsByPath.get(path)!.add(edit.id)

    this.emit({ type: 'edit_added', edit })
    return edit
  }

  /**
   * Mark an edit as applied
   *
   * @param id - Edit ID
   * @returns The updated edit, or undefined if not found
   */
  apply(id: string): PendingEdit | undefined {
    const edit = this.edits.get(id)
    if (!edit || edit.status !== 'pending') {
      return undefined
    }

    edit.status = 'applied'
    edit.resolvedAt = Date.now()

    this.emit({ type: 'edit_applied', edit })
    return edit
  }

  /**
   * Mark an edit as rejected
   *
   * @param id - Edit ID
   * @returns The updated edit, or undefined if not found
   */
  reject(id: string): PendingEdit | undefined {
    const edit = this.edits.get(id)
    if (!edit || edit.status !== 'pending') {
      return undefined
    }

    edit.status = 'rejected'
    edit.resolvedAt = Date.now()

    this.emit({ type: 'edit_rejected', edit })
    return edit
  }

  /**
   * Get an edit by ID
   */
  get(id: string): PendingEdit | undefined {
    return this.edits.get(id)
  }

  /**
   * Delete an edit
   */
  delete(id: string): boolean {
    const edit = this.edits.get(id)
    if (!edit) {
      return false
    }

    this.edits.delete(id)

    // Remove from path index
    const pathEdits = this.editsByPath.get(edit.path)
    if (pathEdits) {
      pathEdits.delete(id)
      if (pathEdits.size === 0) {
        this.editsByPath.delete(edit.path)
      }
    }

    return true
  }

  // ==========================================================================
  // Query Methods
  // ==========================================================================

  /**
   * Get all pending edits
   */
  getPending(): PendingEdit[] {
    return [...this.edits.values()].filter((e) => e.status === 'pending')
  }

  /**
   * Get all edits with a specific status
   */
  getByStatus(status: EditStatus): PendingEdit[] {
    return [...this.edits.values()].filter((e) => e.status === status)
  }

  /**
   * Get all edits for a specific file path
   */
  getByPath(path: string): PendingEdit[] {
    const ids = this.editsByPath.get(path)
    if (!ids) {
      return []
    }
    return [...ids].map((id) => this.edits.get(id)!).filter(Boolean)
  }

  /**
   * Get all edits
   */
  getAll(): PendingEdit[] {
    return [...this.edits.values()]
  }

  /**
   * Check if there are any pending edits
   */
  hasPending(): boolean {
    return this.getPending().length > 0
  }

  /**
   * Get count of edits by status
   */
  getCounts(): Record<EditStatus, number> {
    const counts: Record<EditStatus, number> = {
      pending: 0,
      applied: 0,
      rejected: 0,
    }

    for (const edit of this.edits.values()) {
      counts[edit.status]++
    }

    return counts
  }

  /**
   * Get total diff stats across all pending edits
   */
  getPendingStats(): { additions: number; deletions: number; files: number } {
    const pending = this.getPending()
    let additions = 0
    let deletions = 0

    for (const edit of pending) {
      const stats = getDiffStats(edit.diff)
      additions += stats.additions
      deletions += stats.deletions
    }

    return {
      additions,
      deletions,
      files: pending.length,
    }
  }

  // ==========================================================================
  // Bulk Operations
  // ==========================================================================

  /**
   * Apply all pending edits
   */
  applyAll(): PendingEdit[] {
    const pending = this.getPending()
    const applied: PendingEdit[] = []

    for (const edit of pending) {
      const result = this.apply(edit.id)
      if (result) {
        applied.push(result)
      }
    }

    return applied
  }

  /**
   * Reject all pending edits
   */
  rejectAll(): PendingEdit[] {
    const pending = this.getPending()
    const rejected: PendingEdit[] = []

    for (const edit of pending) {
      const result = this.reject(edit.id)
      if (result) {
        rejected.push(result)
      }
    }

    return rejected
  }

  /**
   * Clear all edits
   */
  clear(): void {
    this.edits.clear()
    this.editsByPath.clear()
    this.emit({ type: 'edits_cleared' })
  }

  /**
   * Clear resolved (applied/rejected) edits
   */
  clearResolved(): number {
    let count = 0

    for (const [id, edit] of this.edits) {
      if (edit.status !== 'pending') {
        this.delete(id)
        count++
      }
    }

    return count
  }

  // ==========================================================================
  // Event Handling
  // ==========================================================================

  /**
   * Subscribe to tracker events
   */
  subscribe(listener: DiffTrackerListener): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  private emit(event: DiffTrackerEvent): void {
    for (const listener of this.listeners) {
      try {
        listener(event)
      } catch {
        // Ignore listener errors
      }
    }
  }

  // ==========================================================================
  // Utilities
  // ==========================================================================

  private generateId(): string {
    return `edit-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`
  }
}

// ============================================================================
// Singleton Instance
// ============================================================================

let defaultTracker: DiffTracker | null = null

/**
 * Get the default diff tracker instance
 */
export function getDefaultTracker(): DiffTracker {
  if (!defaultTracker) {
    defaultTracker = new DiffTracker()
  }
  return defaultTracker
}

/**
 * Reset the default tracker (mainly for testing)
 */
export function resetDefaultTracker(): void {
  defaultTracker = null
}
