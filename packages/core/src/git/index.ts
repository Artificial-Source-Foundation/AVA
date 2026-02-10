/**
 * Git System
 * Git snapshots and history tracking
 */

// Auto-commit (after tool execution)
export { autoCommitIfEnabled, getAutoCommitCount, undoLastAutoCommit } from './auto-commit.js'
// Snapshots
export {
  clearSnapshots,
  createQuickSnapshot,
  createSnapshot,
  deleteSnapshot,
  getAllSnapshots,
  getHistoryAsSnapshots,
  getRecentSnapshots,
  getSnapshot,
  getSnapshotsForPath,
  isSnapshotValid,
  rollback,
  rollbackToId,
} from './snapshot.js'
// Types
export type {
  CreateSnapshotOptions,
  FileStatus,
  GitCommit,
  GitConfig,
  GitResult,
  HistoryOptions,
  RepoState,
  RollbackResult,
  Snapshot,
} from './types.js'
export { DEFAULT_GIT_CONFIG } from './types.js'

// Git utilities
export {
  commit,
  discardChanges,
  escapeForGit,
  execGit,
  fileHasChanges,
  getCurrentBranch,
  getFileStatuses,
  getHeadSha,
  getHistory,
  getRepoRoot,
  getRepoState,
  isDirty,
  isGitRepo,
  resolveRef,
  restoreFromCommit,
  stageFiles,
  unstageFiles,
} from './utils.js'
