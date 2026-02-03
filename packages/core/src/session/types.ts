/**
 * Session Types
 * Types for session state management and persistence
 */

import type { TokenStats } from '../context/tracker.js'
import type { Message } from '../context/types.js'

// ============================================================================
// File State
// ============================================================================

/**
 * State of a file being tracked in a session
 */
export interface FileState {
  /** Absolute path to the file */
  path: string
  /** File content (may be partial for large files) */
  content: string
  /** Last modification time */
  mtime: number
  /** Whether file has unsaved changes */
  dirty: boolean
  /** Language ID for syntax highlighting */
  languageId?: string
  /** Line count (cached) */
  lineCount?: number
}

// ============================================================================
// Checkpoint Types
// ============================================================================

/**
 * A snapshot of session state for rollback
 */
export interface Checkpoint {
  /** Unique checkpoint identifier */
  id: string
  /** When checkpoint was created */
  timestamp: number
  /** Git commit SHA if available */
  gitSha?: string
  /** User-provided description */
  description: string
  /** Number of messages at this checkpoint */
  messageCount: number
  /** Serialized session state (compressed) */
  stateSnapshot?: string
}

/**
 * Checkpoint metadata for listing
 */
export interface CheckpointMeta {
  id: string
  timestamp: number
  description: string
  messageCount: number
}

// ============================================================================
// Session State
// ============================================================================

/**
 * Complete state of a session
 */
export interface SessionState {
  /** Unique session identifier */
  id: string
  /** User-friendly session name */
  name?: string
  /** Conversation messages */
  messages: Message[]
  /** Current working directory */
  workingDirectory: string
  /** Total tool calls made in session */
  toolCallCount: number
  /** Current token statistics */
  tokenStats: TokenStats
  /** Files currently open/tracked */
  openFiles: Map<string, FileState>
  /** Environment variables set during session */
  env: Record<string, string>
  /** Most recent checkpoint (if any) */
  checkpoint?: Checkpoint
  /** All checkpoint IDs for this session */
  checkpointIds?: string[]
  /** When session was created */
  createdAt: number
  /** When session was last updated */
  updatedAt: number
  /** Session status */
  status: 'active' | 'paused' | 'completed' | 'error'
  /** Error message if status is 'error' */
  errorMessage?: string
}

/**
 * Session metadata for listing (without full message history)
 */
export interface SessionMeta {
  id: string
  name?: string
  messageCount: number
  workingDirectory: string
  createdAt: number
  updatedAt: number
  status: SessionState['status']
}

// ============================================================================
// Session Events
// ============================================================================

/**
 * Event emitted when session state changes
 */
export type SessionEvent =
  | { type: 'message_added'; messageId: string; sessionId: string }
  | { type: 'message_removed'; messageId: string; sessionId: string }
  | { type: 'checkpoint_created'; checkpointId: string; sessionId: string }
  | { type: 'checkpoint_restored'; checkpointId: string; sessionId: string }
  | { type: 'session_saved'; sessionId: string }
  | { type: 'session_loaded'; sessionId: string }
  | { type: 'session_cleared'; sessionId: string }
  | { type: 'status_changed'; status: SessionState['status']; sessionId: string }

/**
 * Listener for session events
 */
export type SessionEventListener = (event: SessionEvent) => void

// ============================================================================
// Serialization Types
// ============================================================================

/**
 * Serialized session state for storage
 * Uses arrays instead of Maps for JSON compatibility
 */
export interface SerializedSessionState {
  id: string
  name?: string
  messages: Message[]
  workingDirectory: string
  toolCallCount: number
  tokenStats: {
    messages: [string, number][] // Array of [id, count] pairs
    total: number
    limit: number
    remaining: number
    percentUsed: number
  }
  openFiles: [string, FileState][] // Array of [path, state] pairs
  env: Record<string, string>
  checkpoint?: Checkpoint
  checkpointIds?: string[]
  createdAt: number
  updatedAt: number
  status: SessionState['status']
  errorMessage?: string
}

// ============================================================================
// Manager Options
// ============================================================================

/**
 * Configuration for SessionManager
 */
export interface SessionManagerConfig {
  /** Maximum sessions to keep in memory (LRU cache) */
  maxSessions?: number
  /** Auto-save interval in milliseconds (0 to disable) */
  autoSaveInterval?: number
  /** Storage backend for persistence */
  storage?: SessionStorage
  /** Whether to compress checkpoint snapshots */
  compressCheckpoints?: boolean
}

/**
 * Storage backend interface for session persistence
 */
export interface SessionStorage {
  /** Save a session to storage */
  save(session: SerializedSessionState): Promise<void>
  /** Load a session from storage */
  load(sessionId: string): Promise<SerializedSessionState | null>
  /** Delete a session from storage */
  delete(sessionId: string): Promise<void>
  /** List all session IDs */
  list(): Promise<string[]>
  /** Save a checkpoint */
  saveCheckpoint(sessionId: string, checkpoint: Checkpoint): Promise<void>
  /** Load a checkpoint */
  loadCheckpoint(sessionId: string, checkpointId: string): Promise<Checkpoint | null>
  /** Delete a checkpoint */
  deleteCheckpoint(sessionId: string, checkpointId: string): Promise<void>
  /** List checkpoints for a session */
  listCheckpoints(sessionId: string): Promise<CheckpointMeta[]>
}
