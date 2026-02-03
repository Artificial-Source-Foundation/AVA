/**
 * @estela/core Session Module
 * Session state management, persistence, and checkpoints
 */

// Manager
export { createSessionManager, SessionManager } from './manager.js'
// Types
export type {
  Checkpoint,
  CheckpointMeta,
  FileState,
  ForkInfo,
  ForkOptions,
  SerializedSessionState,
  SessionEvent,
  SessionEventListener,
  SessionManagerConfig,
  SessionMeta,
  SessionState,
  SessionStorage,
  TodoItem,
} from './types.js'
