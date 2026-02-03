/**
 * Diff System
 * Unified diff tracking for file edits
 */

// Tracker
export { DiffTracker, getDefaultTracker, resetDefaultTracker } from './tracker.js'
// Types
export type {
  CreateEditOptions,
  DiffHunk,
  DiffStats,
  DiffTrackerEvent,
  DiffTrackerListener,
  EditStatus,
  PendingEdit,
} from './types.js'

// Unified diff utilities
export {
  createDiff,
  createDiffWithHeaders,
  extractPaths,
  formatDiffLines,
  getDiffStats,
  hasChanges,
  parseDiffHunks,
} from './unified.js'
