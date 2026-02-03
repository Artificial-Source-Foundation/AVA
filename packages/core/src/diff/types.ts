/**
 * Diff System Types
 * Types for unified diff tracking and pending edits
 */

// ============================================================================
// Diff Types
// ============================================================================

/** A hunk from a unified diff */
export interface DiffHunk {
  /** Starting line in original file */
  oldStart: number
  /** Number of lines in original */
  oldLines: number
  /** Starting line in modified file */
  newStart: number
  /** Number of lines in modified */
  newLines: number
  /** Diff lines with +/- prefixes */
  lines: string[]
}

/** Statistics about a diff */
export interface DiffStats {
  /** Lines added */
  additions: number
  /** Lines removed */
  deletions: number
  /** Total hunks */
  hunks: number
}

// ============================================================================
// Edit Types
// ============================================================================

/** Status of a pending edit */
export type EditStatus = 'pending' | 'applied' | 'rejected'

/** A pending file edit with diff */
export interface PendingEdit {
  /** Unique identifier */
  id: string
  /** File path */
  path: string
  /** Original file content */
  original: string
  /** Modified file content */
  modified: string
  /** Unified diff string */
  diff: string
  /** Current status */
  status: EditStatus
  /** When the edit was created */
  createdAt: number
  /** When the edit was applied/rejected */
  resolvedAt?: number
  /** Optional description of the change */
  description?: string
}

/** Options for creating an edit */
export interface CreateEditOptions {
  /** Optional description */
  description?: string
  /** Custom ID (auto-generated if not provided) */
  id?: string
}

// ============================================================================
// Tracker Events
// ============================================================================

/** Events emitted by the diff tracker */
export type DiffTrackerEvent =
  | { type: 'edit_added'; edit: PendingEdit }
  | { type: 'edit_applied'; edit: PendingEdit }
  | { type: 'edit_rejected'; edit: PendingEdit }
  | { type: 'edits_cleared' }

/** Listener for tracker events */
export type DiffTrackerListener = (event: DiffTrackerEvent) => void
