/**
 * Git System Types
 * Types for git snapshots and history tracking
 */

// ============================================================================
// Configuration
// ============================================================================

/** Git integration configuration */
export interface GitConfig {
  /** Whether git integration is enabled */
  enabled: boolean
  /** Automatically commit before modifications */
  autoCommit: boolean
  /** Prefix for AVA-created branches */
  branchPrefix: string
  /** Default commit message prefix */
  messagePrefix: string
}

/** Default git configuration */
export const DEFAULT_GIT_CONFIG: GitConfig = {
  enabled: true,
  autoCommit: false,
  branchPrefix: 'ava/',
  messagePrefix: '[ava]',
}

// ============================================================================
// Snapshot Types
// ============================================================================

/** A git snapshot representing a point in history */
export interface Snapshot {
  /** Unique identifier for this snapshot */
  id: string
  /** Git commit SHA */
  sha: string
  /** Branch name when snapshot was created */
  branch: string
  /** Commit message */
  message: string
  /** File paths included in this snapshot */
  paths: string[]
  /** When the snapshot was created */
  createdAt: number
  /** Optional description */
  description?: string
}

/** Options for creating a snapshot */
export interface CreateSnapshotOptions {
  /** Custom commit message */
  message?: string
  /** Description for the snapshot */
  description?: string
  /** Whether to stage and commit files */
  commit?: boolean
}

// ============================================================================
// Repository State
// ============================================================================

/** Current state of the git repository */
export interface RepoState {
  /** Whether we're in a git repository */
  isRepo: boolean
  /** Current branch name */
  branch: string
  /** Current HEAD SHA */
  sha: string
  /** Whether there are uncommitted changes */
  isDirty: boolean
  /** Root directory of the repository */
  root: string
}

/** Status of a file in the working tree */
export interface FileStatus {
  /** File path relative to repo root */
  path: string
  /** Status code (M, A, D, ??, etc.) */
  status: 'modified' | 'added' | 'deleted' | 'untracked' | 'renamed' | 'copied'
  /** Original path for renamed/copied files */
  originalPath?: string
}

// ============================================================================
// History Types
// ============================================================================

/** A commit from git history */
export interface GitCommit {
  /** Commit SHA */
  sha: string
  /** Short SHA (first 7 chars) */
  shortSha: string
  /** Commit message */
  message: string
  /** Author name */
  author: string
  /** Author email */
  email: string
  /** Commit timestamp */
  timestamp: number
}

/** Options for querying history */
export interface HistoryOptions {
  /** Maximum number of commits to return */
  limit?: number
  /** Only include commits affecting these paths */
  paths?: string[]
  /** Start from this ref */
  since?: string
  /** Only include commits by this author */
  author?: string
}

// ============================================================================
// Operation Results
// ============================================================================

/** Result of a git operation */
export interface GitResult {
  /** Whether the operation succeeded */
  success: boolean
  /** Output from the command */
  output: string
  /** Error message if failed */
  error?: string
}

/** Result of a rollback operation */
export interface RollbackResult extends GitResult {
  /** Files that were restored */
  restoredFiles: string[]
  /** Files that couldn't be restored */
  failedFiles: string[]
}
